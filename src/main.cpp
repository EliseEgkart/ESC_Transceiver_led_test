#include <Arduino.h>
#include "PinChangeInterrupt.h"

//─────────────────────────────────────────────────────────────
// 핀 설정
//─────────────────────────────────────────────────────────────
constexpr uint8_t RED_PIN     = 9;
constexpr uint8_t GREEN_PIN   = 10;
constexpr uint8_t BLUE_PIN    = 11;
constexpr uint8_t LED_PIN1    = 6;  // 밝기 조절용
constexpr uint8_t LED_PIN2    = 5;  // ON/OFF 용

constexpr uint8_t CH9_PIN     = A0;
constexpr uint8_t CH3_PIN     = A1;
constexpr uint8_t CH1_PIN     = A2;

//─────────────────────────────────────────────────────────────
// PWM 기준값 정의
//─────────────────────────────────────────────────────────────
// PWM 범위 설정
constexpr uint16_t PWM_MIN       = 1000;
constexpr uint16_t PWM_MAX       = 2000;
constexpr uint16_t PWM_NEUTRAL   = (PWM_MIN + PWM_MAX) / 2;
constexpr uint16_t PWM_RANGE     = PWM_MAX - PWM_MIN;

// 색상 구간 자동 분할
constexpr uint16_t COLOR_SECTION = PWM_RANGE / 3;

constexpr uint16_t RED_MAX       = PWM_MIN + COLOR_SECTION;
constexpr uint16_t GREEN_MIN     = RED_MAX + 1;
constexpr uint16_t GREEN_MAX     = GREEN_MIN + COLOR_SECTION;
constexpr uint16_t BLUE_MIN      = GREEN_MAX + 1;

// ON_THRESHOLD = PWM 상위 20% 기준
constexpr uint16_t ON_THRESHOLD  = PWM_MAX - (PWM_RANGE / 5);  // top 20%

constexpr uint16_t PWM_CENTER = 1500;
constexpr uint16_t PWM_SPREAD = 432; // 1932 - 1500 or 1500 - 1068
constexpr uint16_t CH3_PWM_INPUT_MIN = PWM_CENTER - PWM_SPREAD;
constexpr uint16_t CH3_PWM_INPUT_MAX = PWM_CENTER + PWM_SPREAD;


//─────────────────────────────────────────────────────────────
// 채널 상태 변수
//─────────────────────────────────────────────────────────────
struct RCChannel {
  volatile uint16_t pulse_width;
  volatile unsigned long start_time;
  volatile bool updated;

  RCChannel(uint16_t init = PWM_MIN)
    : pulse_width(init), start_time(0), updated(false) {}
};

RCChannel ch9(PWM_MIN), ch3(PWM_NEUTRAL), ch1(PWM_NEUTRAL);

//─────────────────────────────────────────────────────────────
// 인터럽트 핸들러
//─────────────────────────────────────────────────────────────
void handle_pwm_change(RCChannel &ch, uint8_t pin) {
  if (digitalRead(pin) == HIGH) {
    ch.start_time = micros();
  } else if (ch.start_time && !ch.updated) {
    ch.pulse_width = micros() - ch.start_time;
    ch.start_time = 0;
    ch.updated = true;
  }
}

void isr_ch9() { handle_pwm_change(ch9, CH9_PIN); }
void isr_ch3() { handle_pwm_change(ch3, CH3_PIN); }
void isr_ch1() { handle_pwm_change(ch1, CH1_PIN); }

//─────────────────────────────────────────────────────────────
// LED 상태 변수
//─────────────────────────────────────────────────────────────
bool led2_on = false;
int led1_brightness = 0;
int red_val = 0, green_val = 0, blue_val = 0;

//─────────────────────────────────────────────────────────────
// LED 제어 함수
//─────────────────────────────────────────────────────────────
void update_led2_onoff() {
  if (ch9.updated) {
    ch9.updated = false;
    led2_on = (ch9.pulse_width > ON_THRESHOLD);
  }
  digitalWrite(LED_PIN2, led2_on ? HIGH : LOW);
}

void update_led1_brightness() {
  if (ch3.updated) {
    ch3.updated = false;

    if (ch3.pulse_width <= CH3_PWM_INPUT_MIN) {
      led1_brightness = 0;
    } else {
      led1_brightness = map(ch3.pulse_width,
                            CH3_PWM_INPUT_MIN + 1,
                            CH3_PWM_INPUT_MAX,
                            0, 255);
      led1_brightness = constrain(led1_brightness, 0, 255);
    }
  }

  analogWrite(LED_PIN1, led1_brightness);
}



void update_rgb_color() {
  if (!ch1.updated) return;
  ch1.updated = false;

  red_val = green_val = blue_val = 0;

  if (ch1.pulse_width <= RED_MAX) {
    red_val = map(ch1.pulse_width, PWM_MIN, RED_MAX, 255, 0);
  } else if (ch1.pulse_width <= GREEN_MAX) {
    green_val = (ch1.pulse_width <= PWM_NEUTRAL)
                  ? map(ch1.pulse_width, GREEN_MIN, PWM_NEUTRAL, 0, 255)
                  : map(ch1.pulse_width, PWM_NEUTRAL, GREEN_MAX, 255, 0);
  } else if (ch1.pulse_width <= PWM_MAX) {
    blue_val = map(ch1.pulse_width, BLUE_MIN, PWM_MAX, 0, 255);
  }

  analogWrite(RED_PIN,   constrain(red_val, 0, 255));
  analogWrite(GREEN_PIN, constrain(green_val, 0, 255));
  analogWrite(BLUE_PIN,  constrain(blue_val, 0, 255));
}

//─────────────────────────────────────────────────────────────
// 디버그 출력
//─────────────────────────────────────────────────────────────
void print_debug_info() {
  static unsigned long last_print = 0;
  const unsigned long interval = 1000;

  if (millis() - last_print >= interval) {
    last_print = millis();
    Serial.print("CH9: "); Serial.print(ch9.pulse_width);
    Serial.print(" | CH3: "); Serial.print(ch3.pulse_width);
    Serial.print(" | CH1: "); Serial.println(ch1.pulse_width);
  }
}

//─────────────────────────────────────────────────────────────
// 초기화
//─────────────────────────────────────────────────────────────
void initialize_pins() {
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(LED_PIN1, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);

  pinMode(CH9_PIN, INPUT_PULLUP);
  pinMode(CH3_PIN, INPUT_PULLUP);
  pinMode(CH1_PIN, INPUT_PULLUP);

  attachPCINT(digitalPinToPCINT(CH9_PIN), isr_ch9, CHANGE);
  attachPCINT(digitalPinToPCINT(CH3_PIN), isr_ch3, CHANGE);
  attachPCINT(digitalPinToPCINT(CH1_PIN), isr_ch1, CHANGE);
}

//─────────────────────────────────────────────────────────────
// 메인 루프
//─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  initialize_pins();
  Serial.println("⟪ RC PWM LED Controller Initialized ⟫");
}

void loop() {
  print_debug_info();
  update_led2_onoff();
  update_led1_brightness();
  update_rgb_color();
}
