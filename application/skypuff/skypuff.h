/*
    Copyright 2020 Kirill Kostiuchenko	kisel2626@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef SKYPUFF_H
#define SKYPUFF_H

#include <QMediaPlayer>
#include <QMediaPlaylist>
#include <QElapsedTimer>
#include "vescinterface.h"
#include "qmlable_skypuff_types.h"

const int aliveTimerDelay = 333; // milliseconds
const int aliveTimeout = 500;
const int commandTimeout = 300;


/*
 * After vesc is connected, Skypuff will ask configuration and
 * will starts sending alive commands.
 *
 * Received MCU terminal prints will be parsed, CUSTOM_APP_DATA
 * deserialized and translated to QML accessible properties and signals.
 *
 * Sorry for some hardcore stings parsing.
 * It's not too much difference from binary in the end.
 *
 * If no answer within commandTimeout, vesc will be disconnected
 * and error message thrown via vesc interface.
 */
class Skypuff : public QObject
{
    Q_OBJECT

    // I tried to make enum skypuff_state QML accesible, but ..
    Q_PROPERTY(QString state READ getState NOTIFY stateChanged)
    // Translated state
    Q_PROPERTY(QString stateText READ getStateText NOTIFY stateTextChanged)

    // To enable transitions to braking, if pos below or equal braking_length + braking_extension_length
    Q_PROPERTY(bool isBrakingExtensionRange READ isBrakingExtensionRange NOTIFY brakingExtensionRangeChanged)
    // To enable manual_slow buttons
    Q_PROPERTY(bool isBrakingRange READ isBrakingRange NOTIFY brakingRangeChanged)
    Q_PROPERTY(float ropeMeters READ getRopeMeters NOTIFY settingsChanged)
    Q_PROPERTY(float drawnMeters READ getDrawnMeters NOTIFY posChanged)
    Q_PROPERTY(float leftMeters READ getLeftMeters NOTIFY posChanged)
    Q_PROPERTY(float speedMs READ getSpeedMs NOTIFY speedChanged)
    Q_PROPERTY(QString motorMode READ getMotorMode NOTIFY motorModeChanged)
    Q_PROPERTY(float motorKg READ getMotorKg NOTIFY motorKgChanged)
    Q_PROPERTY(float power READ getPower NOTIFY powerChanged)
    Q_PROPERTY(float tempFets READ getTempFets NOTIFY tempFetsChanged)
    Q_PROPERTY(float tempMotor READ getTempMotor NOTIFY tempMotorChanged)
    Q_PROPERTY(float tempBat READ getTempBat NOTIFY tempBatChanged)
    Q_PROPERTY(float whIn READ getWhIn NOTIFY whInChanged)
    Q_PROPERTY(float whOut READ getWhOut NOTIFY whOutChanged)
    // Battery
    Q_PROPERTY(bool isBatteryScaleValid READ isBatteryScaleValid NOTIFY batteryScalesChanged)
    Q_PROPERTY(bool isBatteryBlinking READ isBatteryBlinking NOTIFY batteryBlinkingChanged)
    Q_PROPERTY(bool isBatteryWarning READ isBatteryWarning NOTIFY batteryWarningChanged)
    Q_PROPERTY(float batteryPercents READ getBatteryPercents NOTIFY batteryChanged)
    Q_PROPERTY(float batteryVolts READ getBatteryVolts NOTIFY batteryChanged)
    // Readable fault, empty if none
    Q_PROPERTY(QString fault READ getFaultTranslation NOTIFY faultChanged)
public:
    Skypuff(VescInterface *parent = 0);

    Q_INVOKABLE void sendTerminal(const QString &c) {vesc->commands()->sendTerminalCmd(c);}

    // All this types conversion between C++ and QML is very strange...
    Q_INVOKABLE QMLable_skypuff_config emptySettings() {return QMLable_skypuff_config();}
    Q_INVOKABLE void sendSettings(const QMLable_skypuff_config& cfg);

signals:
    /* It is simple to work with QML text states:
     *
     * DISCONNECTED - VESC is disconnected or connected but state not detected yet
     * UNITIALIZED - Skypuff app is waiting for correct settings
     * BRAKING .. and all skypuff states
     */
    void stateChanged(const QString& newState); // Clear state
    void stateTextChanged(const QString& newStateText);
    void settingsChanged(const QMLable_skypuff_config & cfg);
    void statusChanged(const QString &newStatus, bool isWarning = false);
    void brakingExtensionRangeChanged(const bool isBrakingExtensionRange);
    void brakingRangeChanged(const bool isBrakingRange);
    void posChanged(const float meters);
    void speedChanged(const float ms);
    void motorModeChanged(const QString& newMotorMode);
    void motorKgChanged(const float kg);
    void powerChanged(const float power);
    void tempFetsChanged(const float tempFets);
    void tempMotorChanged(const float tempMotor);
    void tempBatChanged(const float tempBat);
    void whInChanged(const float whIn);
    void whOutChanged(const float whOut);

    // Battery
    void batteryScalesChanged(const bool isValid);
    void batteryBlinkingChanged(const bool isBlinking);
    void batteryWarningChanged(const bool isWarning);
    void batteryChanged(const float percents);

    void faultChanged(const QString& newFault);
protected slots:
    void printReceived(QString str);
    void customAppDataReceived(QByteArray data);
    void portConnectedChanged();
    void logVescDialog(const QString & title, const QString & text);
protected:

    // Parsed messages from prints
    enum MessageType {
        PARAM_TEXT,
        PARAM_POS,
        PARAM_SPEED,
        PARAM_BRAKING,
        PARAM_PULL,
        PARAM_TEMP_FETS,
        PARAM_TEMP_MOTOR,
        PARAM_TEMP_BAT,
        PARAM_WH_IN,
        PARAM_WH_OUT,
        PARAM_FAULT,
        PARAM_V_BAT,
    };
    typedef QPair<MessageType, QStringRef> MessageTypeAndPayload;
    typedef QMap<MessageType, QString> MessagesByType;

    VescInterface *vesc;
    QMediaPlayer *player;
    QMediaPlaylist *playlist;

    int aliveTimerId;
    int aliveTimeoutTimerId;
    int getConfTimeoutTimerId;
    QElapsedTimer aliveResponseDelay;

    // Calculate average alive response
    const int avgN = 10;
    QVector<int> aliveResponseDelays;
    int aliveResponseDelayIndex;
    int sumAliveResponseDelay;
    int alivePings;

    QString lastCmd;

    QMLable_skypuff_config cfg;

    // Updated with SK_COMM_ALIVE
    smooth_motor_mode smoothMotorMode;
    QString motorModeText;
    int curTac;
    float erpm, amps, power;
    float tempFets, tempMotor, tempBat;
    float whIn, whOut;
    float vBat;
    mc_fault_code fault, playingFault;


    // Tons of regexps to parse terminal prints
    QRegExp reBraking, rePull, rePos, reSpeed, rePullingHigh;
    QRegExp reUnwindedFromSlowing, rePrePullTimeout, reMotionDetected;
    QRegExp reTakeoffTimeout;

    QString state;
    QString stateText;
    QString status;

    // Will convert strings to enum values
    QHash<QString, skypuff_state> h_states;
    QHash<QString, mc_fault_code> h_faults;
    QHash<MessageType, QString> messageTypes;

    // Getters
    bool isBrakingRange() const {return abs(curTac) <= cfg.braking_length;}
    bool isBrakingExtensionRange() const {return abs(curTac) <= cfg.braking_length + cfg.braking_extension_length;}
    float getRopeMeters() {return cfg.rope_length_to_meters();}
    float getDrawnMeters() {return cfg.tac_steps_to_meters(abs(curTac));}
    float getLeftMeters() {return cfg.tac_steps_to_meters(cfg.rope_length - abs(curTac));}
    float getSpeedMs() {return cfg.erpm_to_ms(erpm);}
    float getMotorKg() {return amps / cfg.amps_per_kg;}
    float getPower() {return power;}
    float getTempFets() {return tempFets;}
    float getTempMotor() {return tempMotor;}
    float getTempBat() {return tempBat;}
    float getWhIn() {return whIn;}
    float getWhOut() {return whOut;}

    // Battery
    bool isBatteryScaleValid() {return cfg.v_in_max;}
    bool isBatteryWarning();
    bool isBatteryBlinking();
    float getBatteryPercents();
    float getBatteryVolts() {return vBat;}

    QString getState() {return state;}
    QString getStateText() {return stateText;}
    QString getStatus() {return status;}
    QString getMotorMode() {return motorModeText;}
    QString getFaultTranslation();
    void playAudio();

    // emit scales signals
    void scalesUpdated();

    // Setters
    void setState(const QString& newState);
    void setStatus(const QString& mcuStatus);
    void setPos(const int new_pos);
    void setSpeed(const float new_erpm);
    void setMotor(const smooth_motor_mode newMode, const float newAmps, const float newPower);
    void setTempFets(const float newTempFets);
    void setTempMotor(const float newTempMotor);
    void setTempBat(const float newTempBat);
    void setWhIn(const float newWhIn);
    void setWhOut(const float newWhOut);
    void setFault(const mc_fault_code newFault);
    void setVBat(const float newVBat);

    // Helpers
    void sendGetConf();
    void sendAlive();
    void timerEvent(QTimerEvent *event) override;
    bool parsePrintMessage(QStringRef &str, MessageTypeAndPayload &c);
    void processAlive(VByteArray &vb);
    void processSettingsV1(VByteArray &vb);
};

#endif // SKYPUFF_H