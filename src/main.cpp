#include <Arduino.h>
#include "PinChangeInterrupt.h"  // 외부 핀의 변화 감지를 위한 인터럽트 라이브러리

//─────────────────────────────────────────────────────────────
// 핀 설정: RGB 및 단색 LED 출력 핀, RC 채널 입력 핀
//─────────────────────────────────────────────────────────────
constexpr uint8_t RED_PIN     = 9;   // RGB LED - Red 채널 PWM 출력
constexpr uint8_t GREEN_PIN   = 10;  // RGB LED - Green 채널 PWM 출력
constexpr uint8_t BLUE_PIN    = 11;  // RGB LED - Blue 채널 PWM 출력
constexpr uint8_t LED_PIN1    = 6;   // 단색 LED 1 (밝기 제어용) PWM 출력 핀
constexpr uint8_t LED_PIN2    = 5;   // 단색 LED 2 (ON/OFF 제어용) 디지털 출력 핀

constexpr uint8_t CH9_PIN     = A0;  // RC 수신기 채널 9: ON/OFF 스위치
constexpr uint8_t CH3_PIN     = A1;  // RC 수신기 채널 3: 밝기 조절 (조이스틱 수직 방향)
constexpr uint8_t CH1_PIN     = A2;  // RC 수신기 채널 1: 색상 조절 (조이스틱 수직 방향)

//─────────────────────────────────────────────────────────────
// PWM 기준값 정의: RC PWM 신호 범위 및 세부 구간 계산
//─────────────────────────────────────────────────────────────
constexpr uint16_t PWM_MIN       = 1000;               // 최소 PWM 폭 (1ms)
constexpr uint16_t PWM_MAX       = 2000;               // 최대 PWM 폭 (2ms)
constexpr uint16_t PWM_NEUTRAL   = (PWM_MIN + PWM_MAX) / 2;
constexpr uint16_t PWM_RANGE     = PWM_MAX - PWM_MIN;  // PWM 총 범위 (1000us)

// 색상 구간 자동 분할: 3등분하여 RED-GREEN-BLUE로 나눔
constexpr uint16_t COLOR_SECTION = PWM_RANGE / 3;
constexpr uint16_t RED_MAX       = PWM_MIN + COLOR_SECTION;
constexpr uint16_t GREEN_MIN     = RED_MAX + 1;
constexpr uint16_t GREEN_MAX     = GREEN_MIN + COLOR_SECTION;
constexpr uint16_t BLUE_MIN      = GREEN_MAX + 1;

// LED ON/OFF 임계값: 상위 20% 이상이면 ON
constexpr uint16_t ON_THRESHOLD  = PWM_MAX - (PWM_RANGE / 5);

// 채널 3 입력값 범위: 수신기 측정값 기준 (예: 1068~1932us)
constexpr uint16_t PWM_CENTER = 1500;
constexpr uint16_t PWM_SPREAD = 432;                             // 중앙값 기준 offset
constexpr uint16_t CH3_PWM_INPUT_MIN = PWM_CENTER - PWM_SPREAD;  // 1068
constexpr uint16_t CH3_PWM_INPUT_MAX = PWM_CENTER + PWM_SPREAD;  // 1932

//─────────────────────────────────────────────────────────────
// 수신 채널 상태 저장 구조체: 펄스폭, 시작시간, 업데이트 플래그
//─────────────────────────────────────────────────────────────
struct RCChannel {
  volatile uint16_t pulse_width;      // 측정된 PWM 펄스 폭 (us 단위)
  volatile unsigned long start_time;  // HIGH 신호가 시작된 시간
  volatile bool updated;              // 새로운 PWM 신호 수신 여부

  RCChannel(uint16_t init = PWM_MIN)
    : pulse_width(init), start_time(0), updated(false) {}
};

// 각 채널 객체 초기화
RCChannel ch9(PWM_MIN), ch3(PWM_NEUTRAL), ch1(PWM_NEUTRAL);

//─────────────────────────────────────────────────────────────
// 공통 PWM 인터럽트 핸들러: HIGH 시작 → LOW에서 시간 계산
//─────────────────────────────────────────────────────────────
void handle_pwm_change(RCChannel &ch, uint8_t pin) {
  if (digitalRead(pin) == HIGH) {
    ch.start_time = micros();  // HIGH 시작 시간 기록
  } else if (ch.start_time && !ch.updated) {
    ch.pulse_width = micros() - ch.start_time;  // 펄스폭 계산
    ch.start_time = 0;
    ch.updated = true;
  }
}

// 각 채널에 대한 인터럽트 등록용 핸들러
void isr_ch9() { handle_pwm_change(ch9, CH9_PIN); }
void isr_ch3() { handle_pwm_change(ch3, CH3_PIN); }
void isr_ch1() { handle_pwm_change(ch1, CH1_PIN); }

//─────────────────────────────────────────────────────────────
// LED 상태 변수
//─────────────────────────────────────────────────────────────
bool led2_on = false;          // ON/OFF 제어용 LED 상태
int led1_brightness = 0;       // 밝기 제어용 LED PWM 값 (0~255)
int red_val = 0, green_val = 0, blue_val = 0;  // RGB LED 각 색상의 PWM 값

//─────────────────────────────────────────────────────────────
// 채널 9 → ON/OFF 제어 함수 (디지털)
//─────────────────────────────────────────────────────────────
void update_led2_onoff() {
  if (ch9.updated) {
    ch9.updated = false;
    led2_on = (ch9.pulse_width > ON_THRESHOLD);  // 임계값 초과 시 ON
  }
  digitalWrite(LED_PIN2, led2_on ? HIGH : LOW);
}

//─────────────────────────────────────────────────────────────
// 채널 3 → 밝기 제어 함수 (아날로그)
//─────────────────────────────────────────────────────────────
void update_led1_brightness() {
  if (ch3.updated) {
    ch3.updated = false;

    if (ch3.pulse_width <= CH3_PWM_INPUT_MIN) {
      led1_brightness = 0;  // 데드존 이하 → 꺼짐
    } else {
      // PWM 범위를 0~255로 선형 매핑
      led1_brightness = map(ch3.pulse_width,
                            CH3_PWM_INPUT_MIN + 1,
                            CH3_PWM_INPUT_MAX,
                            0, 255);
      led1_brightness = constrain(led1_brightness, 0, 255);
    }
  }

  analogWrite(LED_PIN1, led1_brightness);  // PWM 출력
}

//─────────────────────────────────────────────────────────────
// 채널 1 → RGB 색상 변경 함수 (아날로그)
//─────────────────────────────────────────────────────────────
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

  // 최종 RGB PWM 출력
  analogWrite(RED_PIN,   constrain(red_val, 0, 255));
  analogWrite(GREEN_PIN, constrain(green_val, 0, 255));
  analogWrite(BLUE_PIN,  constrain(blue_val, 0, 255));
}

//─────────────────────────────────────────────────────────────
// 디버그 출력: 시리얼 모니터에 각 채널의 PWM 값 출력
//─────────────────────────────────────────────────────────────
void print_debug_info() {
  static unsigned long last_print = 0;
  const unsigned long interval = 1000;  // 출력 주기 (ms)

  if (millis() - last_print >= interval) {
    last_print = millis();
    Serial.print("CH9: "); Serial.print(ch9.pulse_width);
    Serial.print(" | CH3: "); Serial.print(ch3.pulse_width);
    Serial.print(" | CH1: "); Serial.println(ch1.pulse_width);
  }
}

//─────────────────────────────────────────────────────────────
// 핀 초기화 및 인터럽트 등록
//─────────────────────────────────────────────────────────────
void initialize_pins() {
  // LED 출력 핀 설정
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(LED_PIN1, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);

  // RC 입력 핀 설정
  pinMode(CH9_PIN, INPUT_PULLUP);
  pinMode(CH3_PIN, INPUT_PULLUP);
  pinMode(CH1_PIN, INPUT_PULLUP);

  // 인터럽트 연결
  attachPCINT(digitalPinToPCINT(CH9_PIN), isr_ch9, CHANGE);
  attachPCINT(digitalPinToPCINT(CH3_PIN), isr_ch3, CHANGE);
  attachPCINT(digitalPinToPCINT(CH1_PIN), isr_ch1, CHANGE);
}

//─────────────────────────────────────────────────────────────
// 메인 실행 루프: 반복적으로 채널 상태 갱신 및 출력
//─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);            // 시리얼 통신 시작 (디버깅용)
  initialize_pins();             // 핀 및 인터럽트 초기화
  Serial.println("⟪ RC PWM LED Controller Initialized ⟫");
}

void loop() {
  print_debug_info();            // 디버그 정보 출력
  update_led2_onoff();           // 채널 9 → LED ON/OFF
  update_led1_brightness();      // 채널 3 → 밝기 조절
  update_rgb_color();            // 채널 1 → 색상 변경
}