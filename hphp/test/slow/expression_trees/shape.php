<?hh

<<file:__EnableUnstableFeatures('expression_trees', 'expression_tree_shape_creation')>>

<<__EntryPoint>>
function test(): void {
  require __DIR__.'/../../../hack/test/expr_tree.php';

  print_et(ExampleDsl`shape()`);
  print_et(ExampleDsl`shape('x' => 1, 'y' => shape('z' => 2))`);
  print_et(ExampleDsl`ExampleDsl::shapeAt(shape('x' => 1), 'x')`);
}
