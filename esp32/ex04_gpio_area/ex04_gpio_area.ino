/*******************************************************************************
Example 04: NCIR MLX90614 & TOF Human Body Temperature Checker

・距離センサが人体を検出すると、測定を開始します。
・測定中は緑色LEDが点滅するとともに、測定音を鳴らします。
・測定が完了したときに37.5℃以上だった場合、警告音を鳴らし、赤LEDを点灯します。
・35.0～37.5℃だった場合、ピンポン音を鳴らします。

・対応する非接触温度センサ：
　M5Stack NCIR Non-Contact Infrared Thermometer Sensor Unit
　Melexis MLX90614; Microelectronic Integrated Systems, Infra Red Thermometer

・対応する測距センサ：
　M5Stack Time-of-Flight Distance Ranging Sensor Unit
　STMicroelectronics VL53L0X; Time-of-Flight ranging sensor

                                          Copyright (c) 2020-2021 Wataru KUNINO
********************************************************************************
【ご注意】本ソフトウェアはセンサを使った学習用・実験用のサンプルプログラムです。
・本ソフトウェアで測定、表示する値は体温の目安値です。
・このまま医療やウィルス等の感染防止対策用に使用することは出来ません。
・変換式は、特定の条件下で算出しており、一例として以下の条件を考慮していません。
　- センサの個体差（製造ばらつき）
　- 被験者の個人差（顔の大きさ・形状、髪の量、眼鏡の有無など）
　- 室温による顔の表面温度差（体温と室温との差により、顔の表面温度が変化する）
　- 基準体温36.0℃としていることによる検出体温37.5℃の誤差の増大※

※37.5℃を検出するのであれば、本来は体温37.5℃の人体で近似式を求めるべきですが、
　本ソフトウェアは学習用・実験用につき、約36℃の人体を使用して作成しました。
********************************************************************************
【参考文献】

NCIRセンサ MLX90614 (Melexis製)
    https://www.melexis.com/en/product/MLX90614/
    MLX90614xAA (5V仕様：x=A, 3V仕様：x=B) h=4.1mm 90°

TOFセンサ VL53L0X (STMicroelectronics製) に関する参考文献
    https://groups.google.com/d/msg/diyrovers/lc7NUZYuJOg/ICPrYNJGBgAJ
*******************************************************************************/

#include <Wire.h>                               // I2C通信用ライブラリ
#define LED_RED_PIN   16                        // 赤色LEDのIOポート番号
#define LED_GREEN_PIN 17                        // 緑色LEDのIOポート番号
#define BUZZER_PIN    25                        // IO 25にスピーカを接続
#define VOL 3                                   // スピーカ用の音量(0～10)
#ifndef PI
    #define PI 3.1415927                        // 円周率
#endif
#define FOV 90.                                 // センサの半値角

float Sobj = 100. * 70. * PI;                   // 測定対象の面積(mm2)
float TempOfsAra = (273.15 + 36) * 0.02;        // 皮膚からの熱放射時の減衰
float temp_sum = 0.0;                           // 体温値の合計(平均計算用)
int temp_count = 0;                             // temp_sumの測定済サンプル数

/* スピーカ出力用 LEDC */
#define LEDC_CHANNEL_0     0    // use first channel of 16 channels (started from zero)
#define LEDC_TIMER_13_BIT  13   // use 13 bit precission for LEDC timer
#define LEDC_BASE_FREQ     5000 // use 5000 Hz as a LEDC base frequency

void beepSetup(int PIN){
    pinMode(BUZZER_PIN,OUTPUT);                 // スピーカのポートを出力に
    Serial.print("ledSetup LEDC_CHANNEL_0 = ");
    Serial.print(LEDC_CHANNEL_0);
    Serial.print(", BUZZER_PIN = ");
    Serial.print(BUZZER_PIN);
    Serial.print(", freq. = ");
    Serial.println(ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT),3);
    ledcAttachPin(PIN, LEDC_CHANNEL_0);
}

void beep(int freq = 880, int t = 100){         // ビープ音を鳴らす関数
    ledcWriteTone(0, freq);                     // PWM出力を使って音を鳴らす
    for(int duty = 50; duty > 1; duty /= 2){    // PWM出力のDutyを減衰させる
        ledcWrite(0, VOL * duty / 10);          // 音量を変更する
        delay(t / 6);                           // 0.1秒(100ms)の待ち時間処理
    }
    ledcWrite(0, 0);                            // ビープ鳴音の停止
}

void beep_chime(){                              // チャイム音を鳴らす関数
    beep(1109, 600);                            // ピーン音(1109Hz)を0.6秒再生
    delay(300);                                 // 0.3秒(300ms)の待ち時間処理
    beep(880, 100);                             // ポン音(880Hz)を0.6秒再生
}

void beep_alert(int num = 3){
    for(; num > 0 ; num--) for(int i = 2217; i > 200; i /= 2) beep(i);
}

void setup(){                                   // 起動時に一度だけ実行する関数
    Serial.begin(115200);                       // シリアル通信速度を設定する
    beepSetup(BUZZER_PIN);                      // ブザー用するPWM制御部の初期化
    pinMode(LED_RED_PIN, OUTPUT);               // GPIO 18 を赤色LED用に設定
    pinMode(LED_GREEN_PIN, OUTPUT);             // GPIO 19 を緑色LED用に設定
    Wire.begin();                               // I2Cを初期化
    Serial.println("Example 04: Body Temperature Checker [ToF][LED]");
}

void loop(){                                    // 繰り返し実行する関数
    float Dist = (float)VL53L0X_get();          // 測距センサVL53L0Xから距離取得
    if(Dist <= 20.) return;                     // 20mm以下の時に再測定
    if(Dist > 400){                             // 400mm超のとき
        temp_sum = 0.0;                         // 体温の合計値を0にリセット
        temp_count = 0;                         // 測定サンプル数を0にリセット
        return;                                 // 測定処理を中断
    }
    
    float Tenv= getTemp(6);                     // センサの環境温度を取得
    float Tsen= getTemp();                      // センサの測定温度を取得
    if(Tenv < -20. || Tsen < -20.) return;      // -20℃未満のときは中断
    
    // 体温Tobj = 環境温度 + センサ温度差値×√(センサ測定面積÷測定対象面積)
    float Ssen = pow(Dist * tan(FOV / 360. * PI), 2.) * PI;  // センサ測定面積
    float Tobj = Tenv + TempOfsAra + (Tsen - Tenv) * sqrt(Ssen / Sobj);
    if(Tobj < 0. || Tobj > 99.) return;         // 0℃未満/99℃超過時は戻る
    temp_sum += Tobj;                           // 変数temp_sumに体温を加算
    temp_count++;                               // 測定済サンプル数に1を加算
    float temp_avr = temp_sum / (float)temp_count;  // 体温の平均値を算出
    
    if(temp_count % 5 == 0){
        Serial.printf("ToF=%.0fcm ",Dist/10);   // 測距結果を表示
        Serial.printf("Te=%.1f ",Tenv);         // 環境温度を表示
        Serial.printf("Ts=%.1f ",Tsen);         // 測定温度を表示
        Serial.printf("To=%.1f ",Tobj);         // 物体温度を表示
        Serial.printf("Tavr=%.1f\n",temp_avr);  // 平均温度を表示
        digitalWrite(LED_RED_PIN, LOW);         // LED赤を消灯
        digitalWrite(LED_GREEN_PIN, LOW);       // LED緑を消灯
        beep(1047);                             // 1047Hzのビープ音(測定中)
    }
    if(temp_count % 20 != 0) return;            // 10の剰余が0以外のときに先頭へ
    
    if(temp_avr >= 37.5){                       // 37.5℃以上のとき(発熱検知)
        digitalWrite(LED_RED_PIN, HIGH);        // LED赤を点灯
        digitalWrite(LED_GREEN_PIN, LOW);       // LED緑を消灯
        beep_alert(3);                          // アラート音を3回、鳴らす
    }else if(temp_avr < 35.0){                  // 35.0℃未満のとき(再測定)
        temp_sum = Tobj;                        // 最後の測定結果のみを代入
        temp_count = 1;                         // 測定済サンプル数を1に
    }else{
        digitalWrite(LED_RED_PIN, LOW);         // LED赤を消灯
        digitalWrite(LED_GREEN_PIN, HIGH);      // LED緑を点灯
        beep_chime();                           // ピンポン音を鳴らす
        temp_sum = 0.0;                         // 体温の合計値を0にリセット
        temp_count = 0;                         // 測定サンプル数を0にリセット
        delay(3000);                            // 5秒間、待機する
    }
}
