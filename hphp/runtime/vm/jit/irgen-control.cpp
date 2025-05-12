/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-present Facebook, Inc. (http://www.facebook.com)  |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#include "hphp/runtime/vm/jit/irgen-control.h"

#include "hphp/runtime/vm/unwind.h"

#include "hphp/runtime/vm/jit/switch-profile.h"
#include "hphp/runtime/vm/jit/target-profile.h"

#include "hphp/runtime/vm/jit/irgen-exit.h"
#include "hphp/runtime/vm/jit/irgen-inlining.h"
#include "hphp/runtime/vm/jit/irgen-internal.h"
#include "hphp/runtime/vm/jit/irgen-interpone.h"

#include "hphp/util/configs/debugger.h"
#include "hphp/util/text-util.h"

namespace HPHP::jit::irgen {

TRACE_SET_MOD(hhir)

void surpriseCheck(IRGS& env) {
  auto const exit = makeExitSlow(env);
  gen(env, CheckSurpriseFlags, exit, anyStackRegister(env));
}

void surpriseCheck(IRGS& env, Offset relOffset) {
  if (relOffset <= 0 && !env.skipSurpriseCheck) {
    surpriseCheck(env);
  }
}

void surpriseCheckWithTarget(IRGS& env, Offset targetBcOff) {
  auto const exit = makeExitSurprise(env, SrcKey{curSrcKey(env), targetBcOff});
  gen(env, CheckSurpriseFlags, exit, anyStackRegister(env));
}

/*
 * Returns an IR block corresponding to the given bytecode offset. The block
 * may be a side exit or a normal IR block, depending on whether or not the
 * offset is in the current RegionDesc.
 */
Block* getBlock(IRGS& env, SrcKey sk) {
  // If hasBlock returns true, then IRUnit already has a block for that offset
  // and makeBlock will just return it.  This will be the proper successor
  // block set by setSuccIRBlocks.  Otherwise, the given offset doesn't belong
  // to the region, so we just create an exit block.
  if (!env.irb->hasBlock(sk)) return makeExit(env, sk);

  return env.irb->makeBlock(sk, curProfCount(env));
}

Block* getBlock(IRGS& env, Offset offset) {
  return getBlock(env, SrcKey{curSrcKey(env), offset});
}

//////////////////////////////////////////////////////////////////////

void jmpImpl(IRGS& env, SrcKey sk) {
  auto target = getBlock(env, sk);
  assertx(target != nullptr);
  gen(env, Jmp, target);
}

void jmpImpl(IRGS& env, Offset offset) {
  return jmpImpl(env, SrcKey{curSrcKey(env), offset});
}

void implCondJmp(IRGS& env, Offset taken, bool negate, SSATmp* src) {
  auto const target = getBlock(env, taken);
  assertx(target != nullptr);
  auto const boolSrc = gen(env, ConvTVToBool, src);
  decRef(env, src);
  gen(env, negate ? JmpZero : JmpNZero, target, boolSrc);
}

//////////////////////////////////////////////////////////////////////

void emitJmp(IRGS& env, Offset relOffset) {
  surpriseCheck(env, relOffset);
  jmpImpl(env, bcOff(env) + relOffset);
}

void emitJmpZ(IRGS& env, Offset relOffset) {
  surpriseCheck(env, relOffset);
  auto const takenOff = bcOff(env) + relOffset;
  implCondJmp(env, takenOff, true, popC(env));
}

void emitJmpNZ(IRGS& env, Offset relOffset) {
  surpriseCheck(env, relOffset);
  auto const takenOff = bcOff(env) + relOffset;
  implCondJmp(env, takenOff, false, popC(env));
}

//////////////////////////////////////////////////////////////////////

static const StaticString s_switchProfile("SwitchProfile");

void emitSwitch(IRGS& env, SwitchKind kind, int64_t base,
                const ImmVector& iv) {
  auto bounded = kind == SwitchKind::Bounded;
  int nTargets = bounded ? iv.size() - 2 : iv.size();

  SSATmp* const switchVal = popC(env);
  Type type = switchVal->type();
  assertx(IMPLIES(!(type <= TInt), bounded));
  assertx(IMPLIES(bounded, iv.size() > 2));
  SSATmp* index = switchVal;

  Offset defaultOff = bcOff(env) + iv.vec32()[iv.size() - 1];

  if (UNLIKELY(!(type <= TInt))) {
    if (type <= TArrLike) decRef(env, switchVal);
    gen(env, Jmp, getBlock(env, defaultOff));
    return;
  }

  auto const dataSize = SwitchProfile::extraSize(iv.size());
  TargetProfile<SwitchProfile> profile(
    env.context, env.irb->curMarker(), s_switchProfile.get(), dataSize
  );

  auto checkBounds = [&] {
    if (!bounded) return;
    index = gen(env, SubInt, index, cns(env, base));
    auto const ok = gen(env, CheckRange, index, cns(env, nTargets));
    gen(env, JmpZero, getBlock(env, defaultOff), ok);
    bounded = false;
  };

  // We lower Switch to a series of comparisons if any of the successors are in
  // included in the region.
  auto const offsets = iv.range32();
  auto const shouldLower =
    std::any_of(offsets.begin(), offsets.end(), [&](Offset o) {
      SrcKey sk(curSrcKey(env), bcOff(env) + o);
      return env.irb->hasBlock(sk);
    });
  if (shouldLower && profile.optimizing()) {
    auto const values = sortedSwitchProfile(profile, iv.size());
    FTRACE(2, "Switch profile data for Switch @ {}\n", bcOff(env));
    for (UNUSED auto const& val : values) {
      FTRACE(2, "  case {} hit {} times\n", val.caseIdx, val.count);
    }

    // Emit conditional checks for all successors in this region, in descending
    // order of hotness. We rely on the region selector to decide which arcs
    // are appropriate to include in the region. Fall through to the
    // fully-generic LdSwitchDest at the end if nothing matches.
    for (auto const& val : values) {
      auto targetOff = bcOff(env) + offsets[val.caseIdx];
      SrcKey sk(curSrcKey(env), targetOff);
      if (!env.irb->hasBlock(sk)) continue;

      if (bounded && val.caseIdx == iv.size() - 2) {
        // If we haven't checked bounds yet and this is the "first non-zero"
        // case, we have to skip it. This case is only hit for non-Int input
        // types anyway.
        continue;
      }

      if (val.caseIdx == iv.size() - 1) {
        // Default case.
        checkBounds();
      } else {
        auto ok = gen(env, EqInt, cns(env, val.caseIdx + (bounded ? base : 0)),
                      index);
        gen(env, JmpNZero, getBlock(env, targetOff), ok);
      }
    }
  } else if (profile.profiling()) {
    gen(env, ProfileSwitchDest,
        ProfileSwitchData{profile.handle(), iv.size(), bounded ? base : 0},
        index);
  }

  // Make sure to check bounds, if we haven't yet.
  checkBounds();

  std::vector<SrcKey> targets;
  targets.reserve(iv.size());
  for (auto const offset : offsets) {
    targets.emplace_back(SrcKey{curSrcKey(env), bcOff(env) + offset});
  }

  auto data = LdSwitchData{};
  data.cases = iv.size();
  data.targets = &targets[0];
  data.spOffBCFromStackBase = spOffBCFromStackBase(env);

  auto const target = gen(env, LdSwitchDest, data, index);
  if (isInlining(env)) {
    sideExitFromInlined(env, target);
  } else {
    auto const jmpData = IRSPRelOffsetData { spOffBCFromIRSP(env) };
    gen(env, JmpExit, jmpData, target, sp(env), fp(env));
  }
}

void emitSSwitch(IRGS& env, const ImmVector& iv) {
  const int numCases = iv.size() - 1;
  auto testVal = popC(env);
  auto const defaultOff = bcOff(env) + iv.strvec()[numCases].dest;

 if (UNLIKELY(testVal->isA(TCls) || testVal->isA(TLazyCls))) {
    if (Cfg::Eval::RaiseClassConversionNoticeSampleRate > 0) {
      std::string msg;
      // TODO(vmladenov) appears untested
      string_printf(msg, Strings::CLASS_TO_STRING_IMPLICIT, "string switch");
      gen(env,
        RaiseNotice,
        SampleRateData { Cfg::Eval::RaiseClassConversionNoticeSampleRate },
        cns(env, makeStaticString(msg)));
    }
    testVal = gen(env, testVal->isA(TCls) ? LdClsName : LdLazyClsName, testVal);
  }

  if (UNLIKELY(!testVal->isA(TStr))) {
    // straight to the default
    decRef(env, testVal);
    gen(env, Jmp, getBlock(env, defaultOff));
    return;
  }

  std::vector<LdSSwitchData::Elm> cases(numCases);
  for (int i = 0; i < numCases; ++i) {
    auto const& kv = iv.strvec()[i];
    cases[i].str  = curUnit(env)->lookupLitstrId(kv.str);
    cases[i].dest = SrcKey{curSrcKey(env), bcOff(env) + kv.dest};
  }

  LdSSwitchData data;
  data.numCases   = numCases;
  data.cases      = &cases[0];
  data.defaultSk  = SrcKey{curSrcKey(env), defaultOff};
  data.bcSPOff    = spOffBCFromStackBase(env);

  auto const target = gen(env, LdSSwitchDest, data, testVal);
  decRef(env, testVal);
  if (isInlining(env)) {
    sideExitFromInlined(env, target);
  } else {
    auto const jmpData = IRSPRelOffsetData { spOffBCFromIRSP(env) };
    gen(env, JmpExit, jmpData, target, sp(env), fp(env));
  }
}

void emitThrowNonExhaustiveSwitch(IRGS& env) {
  interpOne(env);
}

void emitRaiseClassStringConversionNotice(IRGS& env) {
  if (Cfg::Eval::RaiseClassConversionNoticeSampleRate > 0) {
    std::string msg;
    string_printf(msg, Strings::CLASS_TO_STRING_IMPLICIT, "bytecode");
    gen(env,
        RaiseNotice,
        SampleRateData { Cfg::Eval::RaiseClassConversionNoticeSampleRate },
        cns(env, makeStaticString(msg)));
  }
}

//////////////////////////////////////////////////////////////////////

void emitSelect(IRGS& env) {
  auto const condSrc = popC(env);
  auto const boolSrc = gen(env, ConvTVToBool, condSrc);
  decRef(env, condSrc);

  ifThenElse(
    env,
    [&] (Block* taken) { gen(env, JmpZero, taken, boolSrc); },
    [&] { // True case
      auto const val = popC(env, DataTypeGeneric);
      popDecRef(env, DecRefProfileId::SelectIfBranch, DataTypeGeneric);
      push(env, val);
    },
    [&] { // False case
      popDecRef(env, DecRefProfileId::SelectElseBranch, DataTypeGeneric);
    }
  );
}

//////////////////////////////////////////////////////////////////////

namespace {

void endCatchImpl(IRGS& env, EndCatchData::CatchMode mode, SSATmp* exc,
                  Optional<IRSPRelOffset> vmspOffset) {
  // If we are unwinding from an inlined function, route the exception
  // to the shared sink.
  if (isInlining(env)) {
    endCatchFromInlined(env, mode, exc);
    return;
  }

  auto const teardown = mode == EndCatchData::CatchMode::LocalsDecRefd
    ? EndCatchData::Teardown::None
    : EndCatchData::Teardown::Full;
  auto const data = EndCatchData {
    spOffBCFromIRSP(env),
    mode,
    EndCatchData::FrameMode::Phplogue,
    teardown,
    vmspOffset
  };
  gen(env, EndCatch, data, fp(env), sp(env), exc);
}

void emitExceptionHandler(IRGS& env, Offset ehOffset, SSATmp* exc) {
  int locId = 0;

  // Forget all frame state information that can't be shared between EHs.
  env.irb->fs().clearForEH();

  // Pop stack items on the top of the stack with unknown values. We don't
  // share DecRefs of these values, as they might be unrelated to each other.
  while (true) {
    auto const curStackPos = spOffBCFromStackBase(env);
    if (curStackPos == spOffEmpty(env)) break;
    if (env.irb->fs().valueOf(Location::Stack{curStackPos}) != nullptr) break;

    popDecRef(env, static_cast<DecRefProfileId>(locId++));
    updateMarker(env);
    env.irb->exceptionStackBoundary();
  }

  std::vector<Block*> ehBlocks;

  auto const ehSrcKey = SrcKey{curSrcKey(env), ehOffset};
  if (auto const block = env.irb->getEHBlock(ehSrcKey)) {
    ehBlocks.push_back(block);
  } else {
    auto const newBlock = defBlock(env, Block::Hint::Unused);
    env.irb->setEHBlock(ehSrcKey, newBlock);
    ehBlocks.push_back(newBlock);
  }

  for (auto i = spOffEmpty(env) + 1; i <= spOffBCFromStackBase(env); ++i) {
    auto const prev = ehBlocks.back();
    auto const value = env.irb->fs().valueOf(Location::Stack{i});
    if (auto const block = env.irb->getEHDecRefBlock(prev, value)) {
      ehBlocks.push_back(block);
    } else {
      auto const newBlock = defBlock(env, Block::Hint::Unused);
      env.irb->setEHDecRefBlock(prev, value, newBlock);
      ehBlocks.push_back(newBlock);
    }
  }

  auto const startBlock = [&](Block* block) {
    env.irb->appendBlock(block);
    auto const label = env.unit.defLabel(1, block, env.irb->nextBCContext());
    exc = label->dst(0);
    exc->setType(Type::SubObj(SystemLib::getThrowableClass()) | TNullptr);
  };

  // Pop the remaining stack items via shared blocks.
  while (true) {
    auto const curStackPos = spOffBCFromStackBase(env);
    if (curStackPos == spOffEmpty(env)) break;

    assertx(!ehBlocks.empty());
    auto const decRefBlock = ehBlocks.back();
    ehBlocks.pop_back();

    gen(env, Jmp, decRefBlock, exc);
    if (!decRefBlock->empty()) return;

    startBlock(decRefBlock);
    popDecRef(env, static_cast<DecRefProfileId>(locId++));
    updateMarker(env);
    env.irb->exceptionStackBoundary();
  }

  assertx(spOffBCFromStackBase(env) == spOffEmpty(env));
  assertx(ehBlocks.size() == 1);
  auto const ehBlock = ehBlocks[0];

  gen(env, Jmp, ehBlock, exc);
  if (!ehBlock->empty()) return;

  startBlock(ehBlock);

  cond(
    env,
    [&](Block* taken) {
      return gen(env, CheckNonNull, taken, exc);
    },
    [&](SSATmp* exception) {
      // Route Hack exceptions to the exception handler.
      push(env, exception);
      jmpImpl(env, ehSrcKey);
      return nullptr;
    },
    [&] {
      // We are throwing a C++ exception, bypassing catch handlers, which would
      // normally clean up iterators. Kill them here, so that once we reach
      // EndInlining, it won't trigger assertions in load-elim.
      auto const numIterators = curFunc(env)->numIterators();
      for (auto i = 0U; i < numIterators; ++i) {
        gen(env, KillIter, IterId{i}, fp(env));
      }

      // Route C++ exceptions to the frame unwinder.
      auto constexpr mode = EndCatchData::CatchMode::UnwindOnly;
      endCatchImpl(env, mode, cns(env, nullptr), std::nullopt);
      return nullptr;
    }
  );
}

}

void emitHandleException(IRGS& env, EndCatchData::CatchMode mode, SSATmp* exc,
                         Optional<IRSPRelOffset> vmspOffset, bool sideEntry) {
  // Stublogues lack proper frames and need special configuration.
  if (env.irb->fs().stublogue()) {
    assertx(!isInlining(env));
    assertx(mode == EndCatchData::CatchMode::UnwindOnly);
    assertx(!sideEntry);
    auto const data = EndCatchData {
      spOffBCFromIRSP(env),
      EndCatchData::CatchMode::UnwindOnly,
      EndCatchData::FrameMode::Stublogue,
      EndCatchData::Teardown::NA,
      std::nullopt
    };
    gen(env, EndCatch, data, fp(env), sp(env), exc);
    return;
  }

  // Teardown::None can't be used without an empty stack.
  assertx(IMPLIES(mode == EndCatchData::CatchMode::LocalsDecRefd,
                  spOffBCFromStackBase(env) == spOffEmpty(env)));
  assertx(IMPLIES(sideEntry, exc->isA(TObj)));

  if (mode == EndCatchData::CatchMode::UnwindOnly) {
    auto const ehOffset = findExceptionHandler(curFunc(env), bcOff(env));
    if (ehOffset != kInvalidOffset) {
      return emitExceptionHandler(env, ehOffset, exc);
    }
  }

  if (sideEntry) {
    gen(env, StUnwinderExn, exc);
    gen(env, StVMFP, fixupFP(env));
    gen(env, StVMPC, cns(env, uintptr_t(env.irb->curMarker().fixupSk().pc())));
    gen(env, StVMReturnAddr, cns(env, 0));
  }
  endCatchImpl(env, mode, exc, std::move(vmspOffset));
}

//////////////////////////////////////////////////////////////////////

void emitThrow(IRGS& env) {
  auto const srcTy = topC(env)->type();
  auto const excTy = Type::SubObj(SystemLib::getExceptionClass());
  auto const errTy = Type::SubObj(SystemLib::getErrorClass());
  auto const maybeThrowable = srcTy.maybe(excTy) || srcTy.maybe(errTy);

  if (!maybeThrowable) return interpOne(env);

  auto slowExit = makeExitSlow(env);
  if (Cfg::Debugger::EnableVSDebugger && Cfg::Eval::EmitDebuggerIntrCheck) {
    irgen::checkDebuggerExceptionIntr(env, slowExit);
  }

  auto const handleException = [&] (SSATmp* exc) {
    popC(env);
    updateMarker(env);

    auto constexpr mode = EndCatchData::CatchMode::UnwindOnly;
    emitHandleException(env, mode, exc, std::nullopt, true /* sideEntry */);
  };

  if (srcTy <= Type::SubObj(SystemLib::getThrowableClass())) {
    return handleException(topC(env));
  }

  handleException(cond(
    env,
    [&] (Block* taken) { return gen(env, CheckType, TObj, taken, topC(env)); },
    [&] (SSATmp* obj) {
      auto const cls = gen(env, LdObjClass, obj);
      return cond(
        env,
        [&] (Block* taken) {
          auto const ecd = ExtendsClassData { SystemLib::getExceptionClass() };
          gen(env, JmpZero, taken, gen(env, ExtendsClass, ecd, cls));
          return gen(env, AssertType, excTy, obj);
        },
        [&] (SSATmp* exc) { return exc; },
        [&] (Block* taken) {
          auto const ecd = ExtendsClassData { SystemLib::getErrorClass() };
          gen(env, JmpZero, taken, gen(env, ExtendsClass, ecd, cls));
          return gen(env, AssertType, errTy, obj);
        },
        [&] (SSATmp* exc) { return exc; },
        [&] {
          gen(env, Jmp, slowExit);
          return cns(env, TBottom);
        }
      );
    },
    [&] {
      gen(env, Jmp, slowExit);
      return cns(env, TBottom);
    }
  ));
}

//////////////////////////////////////////////////////////////////////

}
