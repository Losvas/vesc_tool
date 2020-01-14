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
#include "skypuff.h"

Skypuff::Skypuff(VescInterface *v) : QObject(),
    vesc(v), aliveTimerId(0), commandTimeoutTimerId(0),
    cur_tac(0), erpm(0),
    // Compile regexps once here
    reBraking("(\\d.\\.?\\d*)Kg \\((-?\\d+.?\\d*)A"),
    rePull("(\\d.\\.?\\d*)Kg \\((-?\\d+.?\\d*)A"), // absolute kg, signed amps
    rePos("(\\d.\\.?\\d*)m \\((-?\\d+) steps"), // absolute meters and signed steps
    reSpeed("-?(\\d.\\.?\\d*)ms \\((-?\\d+) ERPM"), // absolute ms and signed erpm
    rePullingHigh("pulling too high .?(\\d+.\\d+)Kg"),
    reUnwindedFromSlowing("Unwinded from slowing zone .?(\\d+.\\d+)m"),
    rePrePullTimeout("Pre pull (\\d+.\\d+)s timeout passed"),
    reMotionDetected("Motion (\\d+.\\d+)m"),
    reTakeoffTimeout("Takeoff (\\d+.\\d+)s")
{
    // Fill message types
    messageTypes[PARAM_TEXT] = "--";
    messageTypes[PARAM_POS] = "pos";
    messageTypes[PARAM_SPEED] = "speed";
    messageTypes[PARAM_BRAKING] = "braking";
    messageTypes[PARAM_PULL] = "pull";

    connect(vesc, SIGNAL(portConnectedChanged()), this, SLOT(portConnectedChanged()));
    connect(vesc->commands(), SIGNAL(printReceived(QString)), this, SLOT(printReceived(QString)));
    connect(vesc->commands(), SIGNAL(customAppDataReceived(QByteArray)), this, SLOT(customAppDataReceived(QByteArray)));

    // Can't use Q_ENUM because skypuff_state declared out of Q_OBJECT class
    QString eof = "UNKNOWN";
    for(int i = 0;;i++) {
        QString s = state_str((skypuff_state)i);
        if(eof == s)
            break;
        h_states[s] = i;
    }

    setState("DISCONNECTED");
}

void Skypuff::setState(const QString& newState)
{
    if(state != newState) {
        state = newState;
        emit stateChanged(state);

        // Translate to UI
        if(!newState.compare("DISCONNECTED"))
            stateText = tr("Disconnected");
        else if(!newState.compare("BRAKING"))
            stateText = tr("Braking");
        else if(!newState.compare("MANUAL_BRAKING"))
            stateText = tr("Manual Braking");
        else if(!newState.compare("BRAKING_EXTENSION"))
            stateText = tr("Braking Extension");
        else if(!newState.compare("UNWINDING"))
            stateText = tr("Unwinding");
        else if(!newState.compare("REWINDING"))
            stateText = tr("Rewinding");
        else if(!newState.compare("SLOWING"))
            stateText = tr("Slowing");
        else if(!newState.compare("SLOW"))
            stateText = tr("Slow");
        else if(!newState.compare("PRE_PULL"))
            stateText = tr("Pre Pull");
        else if(!newState.compare("TAKEOFF_PULL"))
            stateText = tr("Takeoff Pull");
        else if(!newState.compare("PULL"))
            stateText = tr("Pull");
        else if(!newState.compare("FAST_PULL"))
            stateText = tr("Fast Pull");
        else if(!newState.compare("UNITIALIZED"))
            stateText = tr("No Valid Settings");
        else
            stateText = newState;

        emit stateTextChanged(stateText);
    }
}

void Skypuff::portConnectedChanged()
{
    if(!vesc->isPortConnected()) {
        // Stop timers
        if(this->aliveTimerId) {
            this->killTimer(this->aliveTimerId);
            this->aliveTimerId = 0;
        }
        if(this->commandTimeoutTimerId) {
            this->killTimer(this->commandTimeoutTimerId);
            this->commandTimeoutTimerId = 0;
        }

        setState("DISCONNECTED");
    }
    else
        this->sendCmdOrDisconnect("get_conf");
}

void Skypuff::timerEvent(QTimerEvent *event)
{
    if(event->timerId() == this->commandTimeoutTimerId) {
        this->killTimer(this->commandTimeoutTimerId);
        this->commandTimeoutTimerId = 0;
        vesc->emitMessageDialog(tr("Command timeout"),
                                tr("Skypuff command '<i>%1</i>' did not answered within %2ms timeout.<br/><br/>"
                                   "Make sure connection is stable. VESC running Skypuff enabled firmware. "
                                   "Motor configured, Custom User App started. Check it with 'skypuff' command.").arg(this->lastCmd).arg(commandTimeout),
                                true);
        vesc->disconnectPort();
    }
    else if(event->timerId() == this->aliveTimerId) {
        qWarning() << "Skypuff::timerEvent(): alive timer";
    }
    else
        qFatal("Skypuff::timerEvent(): unknown timer id %d", event->timerId());
}

bool Skypuff::sendCmd(const QString &cmd)
{
    if(!this->vesc->isPortConnected()) {
        vesc->emitMessageDialog(tr("Can't send command"),
                                tr("sendCmd(<i>%1</i>) while communication port is disconnected").arg(cmd),
                                true);
        return false;
    }

    if(this->commandTimeoutTimerId) {
        vesc->emitMessageDialog(tr("Can't send command"),
                                tr("sendCmd(<i>%1</i>) while still processing command '%2'..").arg(cmd).arg(this->lastCmd),
                                true);
        return false;
    }

    this->lastCmd = cmd;
    vesc->commands()->sendTerminalCmd(cmd);
    this->commandTimeoutTimerId = this->startTimer(commandTimeout, Qt::PreciseTimer);

    return true;
}

void Skypuff::sendCmdOrDisconnect(const QString &cmd)
{
    if(!this->sendCmd(cmd)) {
        vesc->disconnectPort();
    }
}

// Check the last command is the same or disconnect
bool Skypuff::stopTimout(const QString& cmd)
{
    if(this->lastCmd != cmd) {
        vesc->emitMessageDialog(tr("Incorrect last command"),
                                tr("stopTimeout(<i>%1</i>) while processing command '%2'..").arg(cmd).arg(this->lastCmd),
                                true);
        return false;
    }

    if(!this->commandTimeoutTimerId) {
        vesc->emitMessageDialog(tr("Timeout is not set"),
                                tr("stopTimeout(<i>%1</i>) commandTimeoutTimerId is not set..").arg(cmd),
                                true);
        return false;

    }

    this->killTimer(this->commandTimeoutTimerId);
    this->commandTimeoutTimerId = 0;

    return true;
}

// Parse known command and payload
bool Skypuff::parseCommand(QStringRef &str, MessageTypeAndPayload &c)
{
    int len = str.length();

    if(!len)
        return false;

    int i;

    // Skip spaces or ','
    for(i = 0;(str[i].isSpace() || str[i] == ',') && i < len;i++);

    if(i == len)
        return false;

    if(i)
        str = str.mid(i);

    for(auto ci = messageTypes.constBegin();ci != messageTypes.constEnd();ci++)
        if(str.startsWith(ci.value())) {
            // Payload up to next '--' or end of string
            c.first = ci.key();

            int e = str.indexOf(c.first == PARAM_TEXT ? "--" : ",", ci.value().length());
            if(e == -1)
                e = str.length();

            c.second = str.mid(ci.value().length(), e - ci.value().length()).trimmed();
            str = str.mid(e);

            return true;
        }

    // Unknown command type, just ignore
    return false;
}

void Skypuff::printReceived(QString str)
{
    // Parse state
    int i = str.indexOf(':');

    if(i == -1) {
        qWarning() << "Can't parse state, no colon" << str;
        return;
    }

    QString p_state = str.left(i);

    if(!h_states.contains(p_state)) {
        qWarning() << "Can't parse state, unknown state" << str;
        return;
    }

    QStringRef rStr = str.midRef(i + 1); // Skip ':'
    QStringList messages;

    // Parse messages with types
    MessageTypeAndPayload c;
    while(parseCommand(rStr, c)) {
        switch(c.first) {
        case PARAM_TEXT: // -- Hello baby
            messages.append(c.second.toString());
            break;
        case PARAM_SPEED: // "-0.0ms (-0 ERPM)"
            if(reSpeed.indexIn(c.second.toString()) != -1)
                setSpeed(reSpeed.cap(2).toFloat());
            break;
        case PARAM_POS: // "0.62m (151 steps)"
            if(rePos.indexIn(c.second.toString()) != -1)
                setPos(rePos.cap(2).toFloat());
            break;
        default:
            break;
        }
    };

    // Yeagh, we know the state!
    setState(p_state);

    // Emit messages signals
    if(!messages.isEmpty()) {
        QString title = messages.takeFirst();

        // Only title available?
        if(messages.isEmpty())
            setStatus(title);
        else {
            vesc->emitMessageDialog(title, messages.join("\n"), false);\
        }
    }
}

void Skypuff::customAppDataReceived(QByteArray data)
{
    // Configuration could be set from console
    // get_conf command is not necessary, but possible
    if(commandTimeoutTimerId)
        stopTimout("get_conf");

    VByteArray vb(data);

    if(vb.length() < 1) {
        vesc->emitMessageDialog(tr("Can't deserialize"),
                                tr("Not enough data to deserialize version"),
                                true);
        vesc->disconnectPort();
        return;
    }

    uint8_t version = vb.vbPopFrontUint8();

    if(version != 1) {
        vesc->emitMessageDialog(tr("Wrong config version"),
                                tr("Received skypuff version %1, my version %2.").arg(version).arg(skypuff_config_version),
                                true);
        vesc->disconnectPort();
        return;
    }

    // Enough data?
    const int v1_settings_length = 118;
    if(vb.length() < v1_settings_length) {
        vesc->emitMessageDialog(tr("Can't deserialize V1 settings"),
                                tr("Received %1 bytes, but need %2 bytes!").arg(vb.length()).arg(v1_settings_length),
                                true);
        vesc->disconnectPort();
    }

    // Get state and pos
    skypuff_state mcu_state;
    int new_pos;
    float new_erpm;

    mcu_state = (skypuff_state)vb.vbPopFrontUint8();
    new_pos = vb.vbPopFrontInt32();
    new_erpm = vb.vbPopFrontDouble32Auto();

    cfg.deserializeV1(vb);

    // Update maximum motor current to calculate scales
    cfg.motor_max_current = vesc->mcConfig()->getParamDouble("l_current_max");

    emit settingsChanged(cfg);
    setState(state_str(mcu_state));
    setPos(new_pos, true);
    setSpeed(new_erpm);

    return;
}

bool QMLable_skypuff_config::deserializeV1(VByteArray& from)
{
    if(from.length() < 109)
            return false;

    motor_poles = from.vbPopFrontUint8();
    gear_ratio = from.vbPopFrontDouble32Auto();
    wheel_diameter = from.vbPopFrontDouble32Auto();

    amps_per_kg = from.vbPopFrontDouble32Auto();
    amps_per_sec = from.vbPopFrontDouble32Auto();
    rope_length = from.vbPopFrontInt32();
    braking_length = from.vbPopFrontInt32();
    braking_extension_length = from.vbPopFrontInt32();

    slowing_length = from.vbPopFrontInt32();
    slow_erpm = from.vbPopFrontDouble32Auto();
    rewinding_trigger_length = from.vbPopFrontInt32();
    unwinding_trigger_length = from.vbPopFrontInt32();
    pull_current = from.vbPopFrontDouble32Auto();

    pre_pull_k = from.vbPopFrontDouble32Auto();
    takeoff_pull_k = from.vbPopFrontDouble32Auto();
    fast_pull_k = from.vbPopFrontDouble32Auto();
    takeoff_trigger_length = from.vbPopFrontInt32();
    pre_pull_timeout = from.vbPopFrontInt32();

    takeoff_period = from.vbPopFrontInt32();
    brake_current = from.vbPopFrontDouble32Auto();
    slowing_current = from.vbPopFrontDouble32Auto();
    manual_brake_current = from.vbPopFrontDouble32Auto();
    unwinding_current = from.vbPopFrontDouble32Auto();

    rewinding_current = from.vbPopFrontDouble32Auto();
    slow_max_current = from.vbPopFrontDouble32Auto();
    manual_slow_max_current = from.vbPopFrontDouble32Auto();
    manual_slow_speed_up_current = from.vbPopFrontDouble32Auto();
    manual_slow_erpm = from.vbPopFrontDouble32Auto();

    return true;
}

QByteArray QMLable_skypuff_config::serializeV1() const
{
    VByteArray vb;

    vb.vbAppendUint8(1); // Version

    vb.vbAppendUint8(motor_poles);
    vb.vbAppendDouble32Auto(gear_ratio);
    vb.vbAppendDouble32Auto(wheel_diameter);

    vb.vbAppendDouble32Auto(amps_per_kg);
    vb.vbAppendDouble32Auto(amps_per_sec);
    vb.vbAppendInt32(rope_length);
    vb.vbAppendInt32(braking_length);
    vb.vbAppendInt32(braking_extension_length);
    vb.vbAppendInt32(slowing_length);
    vb.vbAppendDouble32Auto(slow_erpm);
    vb.vbAppendInt32(rewinding_trigger_length);
    vb.vbAppendInt32(unwinding_trigger_length);
    vb.vbAppendDouble32Auto(pull_current);
    vb.vbAppendDouble32Auto(pre_pull_k);
    vb.vbAppendDouble32Auto(takeoff_pull_k);
    vb.vbAppendDouble32Auto(fast_pull_k);
    vb.vbAppendInt32(takeoff_trigger_length);
    vb.vbAppendInt32(pre_pull_timeout);
    vb.vbAppendInt32(takeoff_period);
    vb.vbAppendDouble32Auto(brake_current);
    vb.vbAppendDouble32Auto(slowing_current);
    vb.vbAppendDouble32Auto(manual_brake_current);
    vb.vbAppendDouble32Auto(unwinding_current);
    vb.vbAppendDouble32Auto(rewinding_current);
    vb.vbAppendDouble32Auto(slow_max_current);
    vb.vbAppendDouble32Auto(manual_slow_max_current);
    vb.vbAppendDouble32Auto(manual_slow_speed_up_current);
    vb.vbAppendDouble32Auto(manual_slow_erpm);

    return vb;
}

void Skypuff::sendSettings(const QMLable_skypuff_config& cfg)
{
    vesc->commands()->sendCustomAppData(cfg.serializeV1());
}

// overrideChanged after new seetings to update isBrakingExtensionRange
void Skypuff::setPos(const int new_pos, const bool overrideChanged)
{
    if(new_pos != cur_tac || overrideChanged) {
        bool wasBrakingExtension = isBrakingExtensionRange();

        cur_tac = new_pos;

        bool newBrakingExtension = isBrakingExtensionRange();

        if(wasBrakingExtension != newBrakingExtension)
            emit brakingExtensionRangeChanged(newBrakingExtension);

        emit posChanged(cfg.tac_steps_to_meters(new_pos));
    }
}

void Skypuff::setSpeed(float new_erpm)
{
    if(erpm != new_erpm) {
        erpm = new_erpm;
        emit speedChanged(cfg.erpm_to_ms(new_erpm));
    }
}

// Translate status messages to UI language
void Skypuff::setStatus(const QString& mcuStatus)
{
    QString s = mcuStatus;

    // -- slow pulling too high -1.0Kg (-7.1A) is more 1.0Kg (7.0A)
    if(rePullingHigh.indexIn(s) != -1)
        s = tr("Stopped by pulling %1Kg").arg(rePullingHigh.cap(1));

    // -- Unwinded from slowing zone 4.00m (972 steps)
    else if(reUnwindedFromSlowing.indexIn(s) != -1)
        s = tr("%1m slowing zone unwinded").arg(reUnwindedFromSlowing.cap(1));

    // -- Pre pull 2.0s timeout passed, saving position
    else if(rePrePullTimeout.indexIn(s) != -1)
        s = tr("%1s passed, detecting motion").arg(rePrePullTimeout.cap(1));

    // -- Motion 0.10m (24 steps) detected
    else if(reMotionDetected.indexIn(s) != -1)
        s = tr("%1m motion detected").arg(reMotionDetected.cap(1));

    // -- Takeoff 5.0s timeout passed
    else if(reTakeoffTimeout.indexIn(s) != -1)
        s = tr("%1s takeoff, normal pull").arg(reTakeoffTimeout.cap(1));

    emit statusChanged(s);
}
