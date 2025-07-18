#include <Wire.h>
#include <BH1750.h>

// 조도 센서 객체
BH1750 lightMeter;

// 스테퍼 모터 핀 설정
#define STEP_DIR_PIN 13 // 방향 핀
#define STEP_STP_PIN 12 // 스텝 핀
#define STEP_ENA_PIN 14 // 활성화 핀

// 타이밍 상수
#define STEP_DELAY_MICROS 2500     // 스테퍼 모터 회전 속도 (낮을수록 느림)
#define MEASURE_INTERVAL_MS 10000 * 6   // 초기 조도 측정 간격 (10초 x 6)
#define ROTATE_DURATION_MS 10000   // 밝은 위치를 찾기 위한 최대 회전 시간 (10초)
#define SAMPLE_INTERVAL_MS 500     // 회전 중 조도 샘플링 간격 (0.5초)

// 전역 변수
float previousAverageLux = 0;
bool motorDirection = true; // true는 한 방향, false는 다른 방향 (다음 회전(회귀) 시 사용될 방향)

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // ESP32의 SDA, SCL

  // BH1750 조도 센서 초기화
  if (lightMeter.begin()) {
    Serial.println("BH1750 조도 센서 초기화 완료.");
  } else {
    Serial.println("BH1750 조도 센서 초기화 실패!");
    while (true); // 센서 실패 시 정지
  }

  // 스테퍼 모터 핀 설정
  pinMode(STEP_DIR_PIN, OUTPUT);
  pinMode(STEP_STP_PIN, OUTPUT);
  pinMode(STEP_ENA_PIN, OUTPUT);
  digitalWrite(STEP_ENA_PIN, HIGH); // 초기에는 모터 비활성화
}

void loop() {
  Serial.println("\n조도 측정 시작 (10초 동안 누적 중)...");

  float currentAverageLux = measureAverageLux(MEASURE_INTERVAL_MS);
  Serial.print("📏 현재 평균 조도: ");
  Serial.print(currentAverageLux);
  Serial.println(" lx");

  // 첫 실행 시 previousAverageLux 설정
  if (previousAverageLux == 0) {
    previousAverageLux = currentAverageLux;
    Serial.println("🔁 초기 기준 조도 설정 완료.");
    return;
  }

  // 조도 감소 감지 (10% 감소)
  if (currentAverageLux < previousAverageLux * 0.9) {
    Serial.println("⏬ 조도 감소 감지 → 더 밝은 위치를 찾기 위해 회전 시작.");
    // 중요: 회전 시작 전에 'previousAverageLux'를 현재 측정된 낮은 값으로 업데이트.
    // 이것이 '회전 시작 기준'이 됩니다.
    previousAverageLux = currentAverageLux; // 이 줄을 추가합니다.
    findBrighterSpot();
  } else {
    Serial.println("⏹️ 조도에 큰 변화 없음 → 현재 위치 유지.");
    previousAverageLux = currentAverageLux; // 변화 없으면 현재 평균 조도를 다음 기준값으로 설정
  }

  Serial.println("🕓 다음 측정 주기를 기다리는 중...\n");
}

/**
 * @brief 지정된 시간 동안 평균 조도를 측정합니다.
 * @param durationMs 조도를 측정할 시간 (밀리초)
 * @return 계산된 평균 조도 (럭스)
 */
float measureAverageLux(unsigned long durationMs) {
  float luxSum = 0;
  int sampleCount = 0;
  unsigned long startTime = millis();

  while (millis() - startTime < durationMs) {
    float lux = lightMeter.readLightLevel();
    if (lux != -1) { // 유효한 센서 값인지 확인
      luxSum += lux;
      sampleCount++;
    }
    delay(1000); // 1초마다 샘플링
  }

  return (sampleCount > 0) ? (luxSum / sampleCount) : 0;
}

/**
 * @brief 스테퍼 모터를 회전시켜 가장 밝은 지점을 찾고 해당 위치로 이동합니다.
 * 이 함수는 previousAverageLux를 직접 업데이트하지 않습니다.
 * 다음 loop() 주기에서 새로 측정된 currentAverageLux가 새로운 기준이 됩니다.
 */
void findBrighterSpot() {
  float maxLuxDuringRotation = 0;
  int stepsAtMaxLux = 0;
  int currentRotationSteps = 0; // 현재 회전 동안 누적된 총 스텝 수
  unsigned long rotateStartTime = millis();
  unsigned long lastSampleTime = millis();

  digitalWrite(STEP_ENA_PIN, LOW); // 모터 활성화
  digitalWrite(STEP_DIR_PIN, motorDirection); // 초기 회전 방향 설정

  Serial.print("--- 회전 시작. 초기 모터 방향: ");
  Serial.println(motorDirection ? "정방향" : "역방향"); // 디버깅용: 모터 방향 출력

  while (millis() - rotateStartTime < ROTATE_DURATION_MS) {
    // 한 단계 회전
    digitalWrite(STEP_STP_PIN, HIGH);
    delayMicroseconds(STEP_DELAY_MICROS);
    digitalWrite(STEP_STP_PIN, LOW);
    delayMicroseconds(STEP_DELAY_MICROS);
    currentRotationSteps++; // 현재 회전 중 총 몇 스텝 이동했는지 기록

    // 일정 간격으로 조도 샘플링
    if (millis() - lastSampleTime >= SAMPLE_INTERVAL_MS) {
      float currentLux = lightMeter.readLightLevel();
      if (currentLux != -1) {
        Serial.print("🔎 회전 중 조도: ");
        Serial.print(currentLux);
        Serial.print(" lx (현재 스텝: ");
        Serial.print(currentRotationSteps); // 현재 몇 스텝 움직였는지 출력
        Serial.println(")");

        if (currentLux > maxLuxDuringRotation) {
          maxLuxDuringRotation = currentLux;
          stepsAtMaxLux = currentRotationSteps; // 최대 조도를 기록한 스텝 위치 저장
          Serial.print("⭐️ 새로운 최대 조도 발견: "); // 디버깅용: 새로운 최대 조도 출력
          Serial.print(maxLuxDuringRotation);
          Serial.print(" lx (스텝: ");
          Serial.print(stepsAtMaxLux);
          Serial.println(")");
        }
      }
      lastSampleTime = millis();
    }
  }

  // 10초 회전이 끝난 후 모터는 현재 위치(currentRotationSteps)에 멈춰있음
  digitalWrite(STEP_ENA_PIN, HIGH); // 모터 비활성화

  Serial.println("--- 회전 종료.");
  Serial.print("최종 회전 중 최대 조도: ");
  Serial.print(maxLuxDuringRotation);
  Serial.print(" lx (기록 스텝: ");
  Serial.print(stepsAtMaxLux);
  Serial.println(")");
  Serial.print("이전 평균 조도 (회전 시작 기준): "); // 이전 평균 조도의 의미를 명확히
  Serial.print(previousAverageLux);
  Serial.println(" lx");
  Serial.print("밝음 판단 기준 (이전 평균 조도의 1.3배): "); // 판단 기준도 명확히
  Serial.print(previousAverageLux * 1.3);
  Serial.println(" lx");

  // 가장 밝은 지점에 머무를지 또는 원래 위치로 돌아갈지 결정
  // 현재 위치에서 최대 조도 지점까지 되돌아가야 할 스텝 수 계산
  int stepsToRevert = currentRotationSteps - stepsAtMaxLux;

  if (maxLuxDuringRotation > previousAverageLux * 1.3) {
    Serial.print("✅ 회전 중 훨씬 밝은 위치 (최대 조도: ");
    Serial.print(maxLuxDuringRotation);
    Serial.println(" lx) 발견 → 해당 위치로 이동.");

    // 모터를 최대 조도를 기록했던 위치로 되돌리기
    if (stepsToRevert > 0) { // 되돌릴 스텝이 있는 경우에만 실행
      Serial.print("↩️ 최대 조도 위치로 되돌리는 중 (");
      Serial.print(stepsToRevert);
      Serial.println(" 스텝)...");
      revertMotor(stepsToRevert);
    } else { // 되돌릴 스텝이 0이거나 음수이면(즉, 마지막에 최대값이었거나 이동이 없었으면)
      Serial.println("↩️ 이미 최대 조도 위치에 있거나 되돌릴 필요 없음.");
    }
    // 여기서 previousAverageLux를 직접 업데이트하지 않습니다.
    // 다음 loop() 주기에서 새로운 위치의 실제 평균 조도가 currentAverageLux로 측정되고,
    // 그 값이 previousAverageLux에 반영될 것입니다.
  } else {
    Serial.println("⚠️ 훨씬 밝은 위치를 찾지 못함 → 원래 위치로 되돌아가는 중...");
    // 전체 회전 시작 전의 '원래 위치'로 되돌려야 할 경우, currentRotationSteps만큼 되돌려야 함
    Serial.print("↩️ 초기 위치로 되돌리는 중 (");
    Serial.print(currentRotationSteps);
    Serial.println(" 스텝)...");
    revertMotor(currentRotationSteps); // currentRotationSteps만큼 되돌려 시작 위치로 돌아감
    // 여기도 previousAverageLux를 직접 업데이트하지 않습니다.
    // 다음 loop() 주기에서 원위치된 곳의 실제 평균 조도가 previousAverageLux로 반영됩니다.
  }

  // 다음 회전 시도를 위해 방향 전환
  motorDirection = !motorDirection;
}

/**
 * @brief 스테퍼 모터를 지정된 단계 수만큼 되돌립니다.
 * findBrighterSpot 함수에서 이미 모터 방향을 설정했으므로,
 * 여기서는 현재 motorDirection의 반대 방향으로 되돌립니다.
 * @param steps 되돌릴 단계 수.
 */
void revertMotor(int steps) {
  if (steps <= 0) return; // 되돌릴 스텝이 없으면 아무것도 하지 않음

  digitalWrite(STEP_ENA_PIN, LOW); // 모터 활성화
  // motorDirection 변수는 다음 회전의 시작 방향을 의미하므로,
  // 되돌릴 때는 현재 motorDirection의 반대 방향으로 설정해야 함
  digitalWrite(STEP_DIR_PIN, !motorDirection);

  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_STP_PIN, HIGH);
    delayMicroseconds(STEP_DELAY_MICROS);
    digitalWrite(STEP_STP_PIN, LOW);
    delayMicroseconds(STEP_DELAY_MICROS);
  }
  digitalWrite(STEP_ENA_PIN, HIGH); // 모터 비활성화
}