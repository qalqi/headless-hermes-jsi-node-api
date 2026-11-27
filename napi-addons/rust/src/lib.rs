use napi_derive::napi;

#[napi]
pub fn sum(a: f64, b: f64) -> f64 {
  a + b
}

#[napi]
pub struct Counter {
  value: i32,
}

#[napi]
impl Counter {
  #[napi(constructor)]
  pub fn new(initial: i32) -> Self {
    Self { value: initial }
  }

  #[napi]
  pub fn add(&mut self, delta: i32) -> i32 {
    self.value += delta;
    self.value
  }
}
