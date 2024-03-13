# Toycc Compiler

Toycc compiler is developed based on Rui's [chibicc](https://github.com/rui314/chibicc). While `chibicc` generates x86 assemble language as output target, `toycc` compiles C language and generates RISC-V assemble language.

Toycc supports almost all mandatory features and most optional features of [C11](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) as well as a few GCC language extensions. And is able to compile hundreds of thousands of lines of real-world C code correctly.

## Build Dependencies

Before compiling the project and executing test cases, it is necessary to install dependent software:

~~~
sudo apt-get install gcc-riscv64-linux-gnu qemu-user
~~~

## Build Command

* Compile Toycc Compiler:

~~~
make
~~~

* Run test cases with qemu-user:

~~~
make test
~~~

* Toycc compiler runs self-host:

~~~
make selfhost
~~~

* The self-host compiler runs test cases under qemu:

~~~
make selfhost_test
~~~

* Integrated all functions above:

~~~
make all
~~~

## 内部结构

Toycc 包含以下阶段：

- 词法分析：分词器接受字符串作为输入，拆分处理为标记列表并返回。

- 预处理：预处理器将标记列表作为输入，输出宏拓展后新的标记列表。同时在宏扩展过程中，解析预处理器指令。

- 语法分析：递归派生解析器根据预处理器的输出，构造抽象语法树（AST）。同时也为每个AST节点添加类型。

- 代码生成：代码生成器对给定的AST节点生成汇编文本。
