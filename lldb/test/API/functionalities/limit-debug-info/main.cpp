struct A {
  int a = 47;
  virtual ~A();
};

struct B : A {
  int b = 42;
};

B b;

int foo();

int main() {
  //% self.expect_expr("b.a", result_type="int", result_value="47")
  return 0;
}
