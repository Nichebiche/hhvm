<?hh
//@bento-notebook:{"notebook_number":"N1234","kernelspec":{"display_name":"hack","language":"hack","name":"bento_kernel_hack"}}

//@bento-cell:{"id": 2, "cell_type": "markdown"}
/*@non_hack:
# Check it out

I am a *markdown* **cell**
*/
//@bento-cell-end

// metadata is missing "cell_type"
//@bento-cell:{"id": 1}
class MyClass {
    public function hello(): void {
        echo "hello";
    }
}
//@bento-cell-end
