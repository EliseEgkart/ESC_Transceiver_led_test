#include <Arduino.h>
#include "PinChangeInterrupt.h"  // 핀 레벨 인터럽트를 통한 PWM 신호 수신을 위한 라이브러리

//─────────────────────────────────────────────────────────────
// 핀 설정: 출력용 LED 핀 및 입력용 PWM 채널 핀
//─────────────────────────────────────────────────────────────
constexpr uint8_t RED_PIN     = 9;    // RGB LED - 빨강 채널 (PWM)
constexpr uint8_t GREEN_PIN   = 10;   // RGB LED - 초록 채널 (PWM)
constexpr uint8_t BLUE_PIN    = 11;   // RGB LED - 파랑 채널 (PWM)
constexpr uint8_t LED_PIN1    = 6;    // 단색 LED (밝기 제어용, PWM)
constexpr uint8_t LED_PIN2    = 5;    // 단색 LED (ON/OFF 제어용, 디지털)

// RC 수신기의 각 채널에 연결된 아날로그 핀 (PWM 입력용)
constexpr uint8_t CH9_PIN     = A0;   // 채널 9: LED2 ON/OFF 제어
constexpr uint8_t CH3_PIN     = A1;   // 채널 3: LED1 밝기 제어
constexpr uint8_t CH1_PIN     = A2;   // 채널 1: RGB 색상 제어

//─────────────────────────────────────────────────────────────
// PWM 기준값 정의: RC 수신기로부터 수신되는 PWM 신호 범위
//─────────────────────────────────────────────────────────────
constexpr uint16_t PWM_MIN       = 1000;                 // PWM 신호 최소값 (us)
constexpr uint16_t PWM_MAX       = 2000;                 // PWM 신호 최대값 (us)
constexpr uint16_t PWM_NEUTRAL   = (PWM_MIN + PWM_MAX) / 2;
constexpr uint16_t PWM_RANGE     = PWM_MAX - PWM_MIN;    // 전체 PWM 범위 = 1000us

// RGB 색상 제어를 위한 세 구간 분할
constexpr uint16_t COLOR_SECTION = PWM_RANGE / 3;
constexpr uint16_t RED_MAX       = PWM_MIN + COLOR_SECTION;           // RED 구간 상한
constexpr uint16_t GREEN_MIN     = RED_MAX + 1;                        // GREEN 구간 하한
constexpr uint16_t GREEN_MAX     = GREEN_MIN + COLOR_SECTION;
constexpr uint16_t BLUE_MIN      = GREEN_MAX + 1;                      // BLUE 구간 하한

// ON/OFF 동작을 위한 상위 20% 기준
constexpr uint16_t ON_THRESHOLD  = PWM_MAX - (PWM_RANGE / 5);          // PWM 1800 이상이면 ON

// 채널 3 조이스틱 값 범위 (수동 측정 기반)
constexpr uint16_t PWM_CENTER = 1500;               // 중립값
constexpr uint16_t PWM_SPREAD = 432;                // ±범위
constexpr uint16_t CH3_PWM_INPUT_MIN = PWM_CENTER - PWM_SPREAD;  // 최소 입력값 (1068)
constexpr uint16_t CH3_PWM_INPUT_MAX = PWM_CENTER + PWM_SPREAD;  // 최대 입력값 (1932)

//─────────────────────────────────────────────────────────────
// RC 채널 상태 구조체 정의: PWM 측정 및 상태 갱신용
//─────────────────────────────────────────────────────────────
struct RCChannel {
  volatile uint16_t pulse_width;      // 수신된 PWM 펄스 폭 (us)
  volatile unsigned long start_time;  // HIGH 상태가 시작된 시간
  volatile bool updated;              // 새로운 신호가 들어왔는지 여부

  RCChannel(uint16_t init = PWM_MIN)
    : pulse_width(init), start_time(0), updated(false) {}
};

// 각 채널 상태 인스턴스 생성
RCChannel ch9(PWM_MIN), ch3(PWM_NEUTRAL), ch1(PWM_NEUTRAL);

//─────────────────────────────────────────────────────────────
// PWM 신호의 펄스 폭 측정 인터럽트 핸들러 공통 함수
//─────────────────────────────────────────────────────────────
void handle_pwm_change(RCChannel &ch, uint8_t pin) {
  if (digitalRead(pin) == HIGH) {
    ch.start_time = micros();  // HIGH 신호 시작 시각 저장
  } else if (ch.start_time && !ch.updated) {
    ch.pulse_width = micros() - ch.start_time;  // HIGH 지속 시간 계산 (펄스 폭)
    ch.start_time = 0;
    ch.updated = true;  // 새로운 PWM 값이 갱신되었음을 표시
  }
}

// 각 채널에 대한 인터럽트 핸들러 등록
void isr_ch9() { handle_pwm_change(ch9, CH9_PIN); }
void isr_ch3() { handle_pwm_change(ch3, CH3_PIN); }
void isr_ch1() { handle_pwm_change(ch1, CH1_PIN); }

//─────────────────────────────────────────────────────────────
// LED 상태 변수
//─────────────────────────────────────────────────────────────
bool led2_on = false;          // LED2 ON/OFF 상태 저장
int led1_brightness = 0;       // LED1 밝기 (0~255)
int red_val = 0, green_val = 0, blue_val = 0;  // RGB LED 각 색상값

//─────────────────────────────────────────────────────────────
// 채널 9 → LED2 ON/OFF 제어 함수
//─────────────────────────────────────────────────────────────
void update_led2_onoff() {
  if (ch9.updated) {
    ch9.updated = false;
    led2_on = (ch9.pulse_width > ON_THRESHOLD);  // 기준 초과 시 ON
  }
  digitalWrite(LED_PIN2, led2_on ? HIGH : LOW);  // 디지털 출력
}

//─────────────────────────────────────────────────────────────
// 채널 3 → LED1 밝기 제어 함수 (PWM 기반)
//─────────────────────────────────────────────────────────────
void update_led1_brightness() {
  if (ch3.updated) {
    ch3.updated = false;

    // 조이스틱이 최하단으로 내려갔을 경우 → LED OFF
    if (ch3.pulse_width <= CH3_PWM_INPUT_MIN) {
      led1_brightness = 0;
    } else {
      // PWM 신호를 0~255로 선형 매핑
      led1_brightness = map(ch3.pulse_width,
                            CH3_PWM_INPUT_MIN + 1,
                            CH3_PWM_INPUT_MAX,
                            0, 255);
      // 안전한 범위 제한
      led1_brightness = constrain(led1_brightness, 0, 255);
    }
  }

  analogWrite(LED_PIN1, led1_brightness);  // PWM 출력
}

//─────────────────────────────────────────────────────────────
// 채널 1 → RGB 색상 변경 함수 (PWM 기반 색상 선택)
//─────────────────────────────────────────────────────────────
void update_rgb_color() {
  if (!ch1.updated) return;  // 값이 갱신되지 않았으면 무시
  ch1.updated = false;

  // 초기화: 모든 색상을 먼저 꺼놓고 시작
  red_val = green_val = blue_val = 0;

  // PWM 값이 RED 영역이면 빨강 밝기 감소
  if (ch1.pulse_width <= RED_MAX) {
    red_val = map(ch1.pulse_width, PWM_MIN, RED_MAX, 255, 0);
  }
  // GREEN 영역이면 밝기 증가 → 감소로 분기
  else if (ch1.pulse_width <= GREEN_MAX) {
    green_val = (ch1.pulse_width <= PWM_NEUTRAL)
                  ? map(ch1.pulse_width, GREEN_MIN, PWM_NEUTRAL, 0, 255)
                  : map(ch1.pulse_width, PWM_NEUTRAL, GREEN_MAX, 255, 0);
  }
  // BLUE 영역이면 파란색 밝기 증가
  else if (ch1.pulse_width <= PWM_MAX) {
    blue_val = map(ch1.pulse_width, BLUE_MIN, PWM_MAX, 0, 255);
  }

  // PWM 출력 (0~255)
  analogWrite(RED_PIN,   constrain(red_val, 0, 255));
  analogWrite(GREEN_PIN, constrain(green_val, 0, 255));
  analogWrite(BLUE_PIN,  constrain(blue_val, 0, 255));
}

//─────────────────────────────────────────────────────────────
// 시리얼 모니터용 디버그 출력 함수
//─────────────────────────────────────────────────────────────
void print_debug_info() {
  static unsigned long last_print = 0;
  const unsigned long interval = 1000;  // 출력 주기 1초

  if (millis() - last_print >= interval) {
    last_print = millis();
    Serial.print("CH9: "); Serial.print(ch9.pulse_width);
    Serial.print(" | CH3: "); Serial.print(ch3.pulse_width);
    Serial.print(" | CH1: "); Serial.println(ch1.pulse_width);
  }
}

//─────────────────────────────────────────────────────────────
// 핀 모드 설정 및 인터럽트 등록
//─────────────────────────────────────────────────────────────
void initialize_pins() {
  // 출력 핀 초기화
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(LED_PIN1, OUTPUT);
  pinMode(LED_PIN2, OUTPUT);

  // 입력 핀 초기화 (내부 풀업)
  pinMode(CH9_PIN, INPUT_PULLUP);
  pinMode(CH3_PIN, INPUT_PULLUP);
  pinMode(CH1_PIN, INPUT_PULLUP);

  // 핀 변화 인터럽트 등록
  attachPCINT(digitalPinToPCINT(CH9_PIN), isr_ch9, CHANGE);
  attachPCINT(digitalPinToPCINT(CH3_PIN), isr_ch3, CHANGE);
  attachPCINT(digitalPinToPCINT(CH1_PIN), isr_ch1, CHANGE);
}

//─────────────────────────────────────────────────────────────
// 아두이노 메인 실행 진입점
//─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);          // 시리얼 디버깅용 초기화
  initialize_pins();           // 핀 초기화 및 인터럽트 등록
  Serial.println("⟪ RC PWM LED Controller Initialized ⟫");
}

//─────────────────────────────────────────────────────────────
// 반복 루프: 각 채널 상태를 체크하고 LED 상태 갱신
//─────────────────────────────────────────────────────────────
void loop() {
  print_debug_info();          // PWM 값 출력 (1초 주기)
  update_led2_onoff();         // CH9: ON/OFF 스위치
  update_led1_brightness();    // CH3: 밝기 조절
  update_rgb_color();          // CH1: 색상 조절
}