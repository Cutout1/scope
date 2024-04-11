from machine import Timer, Pin, PWM, ADC, SPI, mem32, freq
import _thread
import time
import data
from ST7735 import TFT
from sysfont import sysfont
import adc_functions

def startDAC():
    global core1
    waveformSetting = settings[4][1][settings[4][0]]
    waveformFreq = settings[5][1][settings[5][0]]
    waveformAmp = settings[6][1][settings[6][0]]
    
    if(waveformFreq == "1Hz"):
        period = 1000000
    elif(waveformFreq == "5Hz"):
        period = 200000
    elif(waveformFreq == "10Hz"):
        period = 100000
    
    if(waveformAmp == "1V"):
        amplitude = 1
    elif(waveformAmp == "2V"):
        amplitude = 2
    elif(waveformAmp == "3V"):
        amplitude = 3
    elif(waveformAmp == "3.3V"):
        amplitude = 3.3
    
    if(waveformSetting == "sine"):
        core1 = _thread.start_new_thread(dac, (sine, period, amplitude))
    if(waveformSetting == "triangle"):
        core1 = _thread.start_new_thread(dac, (tri, period, amplitude))
    if(waveformSetting == "square"):
        core1 = _thread.start_new_thread(dac, ([32768, 65535], period, amplitude))

def dac(waveform, period_us, amplitude):
    global waveDAC, dac_active
    
    num_samples = len(waveform)
    time_step = int(period_us / num_samples)
    
    scaled_waveform = [0]*num_samples
    for x in range(num_samples):
        scaled_waveform[x] = int((waveform[x] - 32768) * amplitude / 3.3 + 32768)
    
    dac_step = 0
    current_time = time.ticks_us()
    while dac_active:
        initial_time = current_time
        waveDAC.duty_u16(int(scaled_waveform[dac_step]))
        dac_step += 1
        if(dac_step == num_samples):
            dac_step = 0
        while(True):
            current_time = time.ticks_us()
            if time.ticks_diff(current_time, initial_time) >= time_step - 1:
                break
    return
        
'''
def startDAC():
    global dacTimer
    waveformSetting = settings[4][1][settings[4][0]]
    if(waveformSetting == "sine"):
        dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=sineDacHandler)
    if(waveformSetting == "triangle"):
        dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=triDacHandler)
    if(waveformSetting == "square"):
        dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=sqrDacHandler)
        
def stopDAC():
    global dacTimer
    dacTimer.deinit()

def sineDacHandler(timer):
    global dacStep, waveDAC
    waveDAC.duty_u16(int(sine[dacStep]))
    if (dacStep != 999):
        dacStep += 1
    else:
        dacStep = 0

def triDacHandler(timer):
    global dacStep, waveDAC
    waveDAC.duty_u16(int(tri[dacStep]))
    if (dacStep != 999):
        dacStep += 1
    else:
        dacStep = 0

def sqrDacHandler(timer):
    global dacStep, waveDAC
    if (dacStep < 500):
        waveDAC.duty_u16(65535)
    else:
        waveDAC.duty_u16(32768)
    if (dacStep != 999):
        dacStep += 1
    else:
        dacStep = 0
'''
def slowMeasure(sample_period_us, trigger, triggerThreshold):
    global C1, C1Ref, C2, C2Ref, ADCDataC1, ADCDataC2, voltageDataC1, voltageDataC2, modeButton
    
    if(trigger == "Rising" or trigger == "Falling"):
        triggerThreshold = int(triggerThreshold * 9929) # 9929 = 65535/6.6
        lastMeasurement = C1.read_u16() - C1Ref.read_u16()
        while(True):
            measurement = C1.read_u16() - C1Ref.read_u16()
            if (lastMeasurement < triggerThreshold and measurement > triggerThreshold and trigger == "Rising") or (lastMeasurement > triggerThreshold and measurement < triggerThreshold and trigger == "Falling"):
                break
            if not modeButton.value():
                return
            lastMeasurement = measurement
        
    current_time = time.ticks_us()
    for x in range(160):
        initial_time = current_time
        ADCDataC1[x] = C1.read_u16() - C1Ref.read_u16()
        ADCDataC2[x] = C2.read_u16() - C2Ref.read_u16()
        while(True):
            current_time = time.ticks_us()
            if time.ticks_diff(current_time, initial_time) >= sample_period_us:
                break
    newVoltageFactor = voltageFactor/16
    for x in range(160):
        voltageDataC1[x] = newVoltageFactor*(ADCDataC1[x])
        voltageDataC2[x] = newVoltageFactor*(ADCDataC2[x])

def fastMeasure(divisor, trigger, triggerThreshold):
    if(trigger == "Switch"):
        return adc_functions.multiSample(4, divisor, 160)
    triggerThreshold = int(triggerThreshold * 621) #621 ~= 4096/6.6
    while True:
        measured_data = adc_functions.multiSample(4, divisor, 320)
        for x in range(160):
            prev = measured_data[4*x] - measured_data[4*x + 1]
            curr = measured_data[4*x + 4] - measured_data[4*x + 5]
            if (trigger == "Rising" and prev < triggerThreshold and curr > triggerThreshold) or (trigger == "Falling" and prev > triggerThreshold and curr < triggerThreshold):
                return measured_data[x*4 + 4:]

def measure():
    global ADCDataC1, ADCDataC2, voltageDataC1, voltageDataC2
    h_scale = settings[1][1][settings[1][0]]
    trigger = settings[2][1][settings[2][0]]
    triggerThreshold = settings[3][1][settings[3][0]]
    triggerThreshold = int(triggerThreshold[:-1])
    measured_data = 0
    if h_scale == "100ms/div":
        slowMeasure(10000, trigger, triggerThreshold)
    if h_scale == "10ms/div":
        slowMeasure(1000, trigger, triggerThreshold)
    if h_scale == "1ms/div":
        #measured_data = adc_functions.multiSample(4, 1200, 160)
        measured_data = fastMeasure(1200, trigger, triggerThreshold)
    if h_scale == "100us/div":
        #measured_data = adc_functions.multiSample(4, 120, 160)
        measured_data = fastMeasure(120, trigger, triggerThreshold)
    if h_scale == "1ms/div" or h_scale == "100us/div":
        for x in range(160):
            ADCDataC1[x] = measured_data[4*x] - measured_data[4*x + 1]
            ADCDataC2[x] = measured_data[4*x + 2] - measured_data[4*x + 3]
            voltageDataC1[x] = voltageFactor*(ADCDataC1[x])
            voltageDataC2[x] = voltageFactor*(ADCDataC2[x])

def plot():
    global yValsC1, yValsC2, ADCDataC1, ADCDataC2, tft, cursorPos
    for x in range(160):
        yValsC1[x] = int(69-displayFactor*voltageDataC1[x])
        yValsC2[x] = int(69-displayFactor*voltageDataC2[x])
        if(yValsC1[x] < 10):
            yValsC1[x] = 10
        if(yValsC2[x] < 10):
            yValsC2[x] = 10
        if(yValsC1[x] > 127):
            yValsC1[x] = 127
        if(yValsC2[x] > 127):
            yValsC2[x] = 127
        if(x > 0):
            tft.line((x-1, yValsC2[x-1]), (x, yValsC2[x]), TFT.BLUE)
            tft.line((x-1, yValsC1[x-1]), (x, yValsC1[x]), TFT.RED)
    cursorPos = 0

def updateText():
    global tft
    h_scale = settings[1][1][settings[1][0]]
    if h_scale == "100ms/div":
        tft.text((0, 0), '{:.2f}s'.format(cursorPos*0.01), TFT.GREEN, sysfont, 1, nowrap=True)
    elif h_scale == "10ms/div":
        tft.text((0, 0), str(cursorPos*1) + "ms  ", TFT.GREEN, sysfont, 1, nowrap=True)
    elif h_scale == "1ms/div":
        tft.text((0, 0), '{:.1f}ms'.format(cursorPos*0.1), TFT.GREEN, sysfont, 1, nowrap=True)
    elif h_scale == "100us/div":
        tft.text((0, 0), '{:.2f}ms'.format(cursorPos*0.01), TFT.GREEN, sysfont, 1, nowrap=True)
    
    tft.text((40, 0), '{:+.2f}V'.format(voltageDataC1[cursorPos]), TFT.RED, sysfont, 1, nowrap=True)
    tft.text((90, 0), '{:+.2f}V'.format(voltageDataC2[cursorPos]), TFT.BLUE, sysfont, 1, nowrap=True)
    waveformSetting = settings[4][1][settings[4][0]]
    if(waveformSetting == "sine"):
        tft.text((140, 0), 'SIN', TFT.YELLOW, sysfont, 1, nowrap=True)
    elif(waveformSetting == "triangle"):
        tft.text((140, 0), 'TRI', TFT.YELLOW, sysfont, 1, nowrap=True)
    elif(waveformSetting == "square"):
        tft.text((140, 0), 'SQR', TFT.YELLOW, sysfont, 1, nowrap=True)

def drawAxes():
    tft.text((6, 8), 'v', TFT.YELLOW, sysfont, 1, nowrap=True)
    tft.text((155, 58), 's', TFT.YELLOW, sysfont, 1, nowrap=True)
    
    tft.vline((0,10), 118, TFT.YELLOW)
    tft.line((0,10), (3, 13), TFT.YELLOW)
    tft.line((0,127), (3, 124), TFT.YELLOW)

    tft.pixel((1,61), TFT.YELLOW)
    tft.pixel((1,53), TFT.YELLOW)
    tft.pixel((1,45), TFT.YELLOW)
    tft.pixel((1,37), TFT.YELLOW)
    tft.pixel((1,29), TFT.YELLOW)
    tft.pixel((1,21), TFT.YELLOW)

    tft.pixel((1,77), TFT.YELLOW)
    tft.pixel((1,85), TFT.YELLOW)
    tft.pixel((1,93), TFT.YELLOW)
    tft.pixel((1,101), TFT.YELLOW)
    tft.pixel((1,109), TFT.YELLOW)
    tft.pixel((1,117), TFT.YELLOW)

    tft.hline((1,69), 159, TFT.YELLOW)
    tft.line((159, 69), (156, 72), TFT.YELLOW)
    tft.line((159, 69), (156, 66), TFT.YELLOW)

    for x in range(15):
        tft.pixel((10 + 10*x,68), TFT.YELLOW)
        tft.pixel((10 + 10*x,70), TFT.YELLOW)

def moveCursor(newPos):
    global cursorPos
    tft.vline((cursorPos, 10), 118, TFT.BLACK)
    drawAxes()
    if(newPos == cursorPos + 1): # moving right
        tft.line((cursorPos, yValsC2[cursorPos]), (newPos, yValsC2[newPos]), TFT.BLUE)
        tft.line((cursorPos, yValsC1[cursorPos]), (newPos, yValsC1[newPos]), TFT.RED)
        if(cursorPos > 0):
            tft.line((cursorPos - 1, yValsC2[cursorPos - 1]), (cursorPos, yValsC2[cursorPos]), TFT.BLUE)
            tft.line((cursorPos - 1, yValsC1[cursorPos - 1]), (cursorPos, yValsC1[cursorPos]), TFT.RED)
    elif(newPos == cursorPos - 1): # moving left
        tft.line((cursorPos, yValsC2[cursorPos]), (newPos, yValsC2[newPos]), TFT.BLUE)
        tft.line((cursorPos, yValsC1[cursorPos]), (newPos, yValsC1[newPos]), TFT.RED)
        if(cursorPos < 159):
            tft.line((cursorPos + 1, yValsC2[cursorPos + 1]), (cursorPos, yValsC2[cursorPos]), TFT.BLUE)
            tft.line((cursorPos + 1, yValsC1[cursorPos + 1]), (cursorPos, yValsC1[cursorPos]), TFT.RED)
    else: # jump
        tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
        tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.RED)
        
    cursorPos = newPos
    #tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
    #tft.pixel((cursorPos, yValsC1[cur  sorPos]),TFT.RED)
    tft.vline((cursorPos, 10), 118, TFT.GREEN)

def openMenu():
    global tft, leftButton, rightButton, leftCounter, rightCounter, selectButton, selectCounter, modeButton, modeCounter, settings, cursorPos, dac_active
    dac_active = False
    tft.fill(TFT.BLACK)
    tft.text((0, 0 ), 'Vert.  Scale:', TFT.WHITE, sysfont, 1, nowrap=True)
    tft.text((0, 12), 'Horiz. Scale:', TFT.WHITE, sysfont, 1, nowrap=True)
    tft.text((0, 24), 'Trigger Type:', TFT.WHITE, sysfont, 1, nowrap=True)
    tft.text((0, 36), 'Trigg. level:', TFT.WHITE, sysfont, 1, nowrap=True)
    tft.text((0, 48), 'Function Out:', TFT.WHITE, sysfont, 1, nowrap=True)
    tft.text((0, 60), 'Function Frq:', TFT.WHITE, sysfont, 1, nowrap=True)
    tft.text((0, 72), 'Function Amp:', TFT.WHITE, sysfont, 1, nowrap=True)
    menuIndex = 0
    updateMenuSelection(menuIndex)
    while(True):
        time.sleep(0.001)
        if (not leftButton.value()):
            leftCounter += 1
            if (leftCounter == 5):
                settings[menuIndex][0] = settings[menuIndex][0] - 1
                if(settings[menuIndex][0] < 0):
                    settings[menuIndex][0] = len(settings[menuIndex][1]) - 1
                updateMenuSelection(menuIndex)
        else:
            leftCounter = 0
    
        if (not rightButton.value()):
            rightCounter += 1
            if (rightCounter == 5):
                settings[menuIndex][0] = settings[menuIndex][0] + 1
                if(settings[menuIndex][0] >= len(settings[menuIndex][1])):
                    settings[menuIndex][0] = 0
                updateMenuSelection(menuIndex)
        else:
            rightCounter = 0
        
        if (not modeButton.value()):
            modeCounter += 1
            if (modeCounter == 5):
                break
        else:
            modeCounter = 0
        
        if (not selectButton.value()):
            selectCounter += 1
            if (selectCounter == 5):
                menuIndex += 1
                if menuIndex == len(settings):
                    break
                updateMenuSelection(menuIndex)
        else:
            selectCounter = 0
    dac_active = True
    startDAC()
    tft.fill(TFT.BLACK)
    drawAxes()
    cursorPos = 0

def updateMenuSelection(menuIndex):
    global tft
    tft.fillrect((80,12*menuIndex - 4), (99, 14), TFT.BLACK)
    tft.hline((80, 12*menuIndex + 9), 6*len(settings[menuIndex][1][settings[menuIndex][0]]), TFT.RED)
    for x in range(len(settings)):
        if x == 3 and settings[2][1][settings[2][0]] == "Switch":
            tft.text((80, 36), "n/a", TFT.GRAY, sysfont, 1, nowrap=True)
        else:
            tft.text((80, x*12), settings[x][1][settings[x][0]] + "      ", TFT.GREEN, sysfont, 1, nowrap=True)

spi = SPI(1, baudrate=133000000, polarity=0, phase=0, sck=Pin(14), mosi=Pin(15), miso=Pin(12))
tft=TFT(spi,10,11,13)
tft.initr()
tft.rgb(True)
tft.rotation(3)
tft.fill(TFT.BLACK)
drawAxes()

sine = data.sineWave()
tri = data.triWave()
wavePin = Pin(20, mode=Pin.OUT)
waveDAC = PWM(wavePin)
waveDAC.freq(1000000)
dacStep = 0

C1Ref = ADC(29)
C1 = ADC(28)
C2Ref = ADC(27)
C2 = ADC(26)

c1gnd = Pin(25, Pin.OUT)
c1gnd.value(0)
c2gnd = Pin(24, Pin.OUT)
c2gnd.value(0)
c3gnd = Pin(23, Pin.OUT)
c3gnd.value(0)
c4gnd = Pin(22, Pin.OUT)
c4gnd.value(0)

thev1k = Pin(4, Pin.OUT)
thev1k.value(1)
thev10k = Pin(5, Pin.OUT)
thev10k.value(1)
thev100k = Pin(6, Pin.OUT)
thev100k.value(1)
thev1M = Pin(7, Pin.OUT)
thev1M.value(1)

voltageFactor = 6.7/4096
displayFactor = 16

ADCDataC1 = [0] * 160
ADCDataC2 = [0] * 160

voltageDataC1 = [0] * 160
voltageDataC2 = [0] * 160

yValsC1 = [69] * 160
yValsC2 = [69] * 160

cursorPos = 0

settings = [
[0, ["0.5V/div", "1.0V/div"]],
[0, ["100ms/div", "10ms/div", "1ms/div", "100us/div"]],
[0, ["Switch", "Rising", "Falling"]],
[2, ["-3V", "-1V", "0V", "1V", "3V"]],
[0, ["sine", "triangle", "square"]],
[0, ["1Hz", "5Hz", "10Hz"]],
[3, ["1V", "2V", "3V", "3.3V"]]
]

leftButton = Pin(0, Pin.IN, Pin.PULL_UP)
rightButton = Pin(1, Pin.IN, Pin.PULL_UP)
modeButton = Pin(2, Pin.IN, Pin.PULL_UP)
selectButton = Pin(3, Pin.IN, Pin.PULL_UP)
startSwitch = Pin(9, Pin.IN, Pin.PULL_UP)
readySwitch = Pin(8, Pin.IN, Pin.PULL_UP)

leftCounter  = 0
rightCounter = 0
modeCounter  = 0
selectCounter = 0
readyCounter = 0
startCounter = 0

leftPressed = False
rightPressed = False

currentSwitchState = "invalid"
lastSwitchState = "none"

if (not readySwitch.value()):
    currentState = "ready"
elif (not startSwitch.value()):
    currentState = "start"
    
lowBatt = Pin(21, Pin.IN)

textUpdateCounter = 0

dac_active = True
startDAC()

while(True):
    time.sleep(0.001)
    textUpdateCounter += 1
    if(textUpdateCounter >= 9):
        textUpdateCounter = 0
        updateText()
    
    if (not leftButton.value()):
        leftCounter += 1
        if (leftCounter == 5):
            leftPressed = True
        elif (leftCounter > 100):
            leftCounter -= 5
            leftPressed = True
    else:
        leftCounter = 0
    
    if (not rightButton.value()):
        rightCounter += 1
        if (rightCounter == 5):
            rightPressed = True
        elif (rightCounter > 100):
            rightCounter -= 5
            rightPressed = True
    else:
        rightCounter = 0
    
    if (not modeButton.value()):
        modeCounter += 1
        if (modeCounter == 5):
            modePressed = True
    else:
        modeCounter = 0
        modePressed = False
    
    if (not selectButton.value()):
        selectCounter += 1
        if (modeCounter == 5):
            selectPressed = True
    else:
        selectCounter = 0
        selectPressed = False
    
    if (not readySwitch.value()):
        readyCounter += 1
        if (readyCounter > 5):
            readyCounter = 0
            currentSwitchState = "ready"
    else:
        readyCounter = 0
    
    if (not startSwitch.value()):
        startCounter += 1
        if (startCounter > 5):
            startCounter = 0
            currentSwitchState = "start"
    else:
        startCounter = 0
    
    # input based actions
    if(leftPressed):
        leftPressed = False
        if(cursorPos == 0):
            moveCursor(159)
        else:
            moveCursor(cursorPos - 1)
        
    if(rightPressed):
        rightPressed = False
        if(cursorPos == 159):
            moveCursor(0)
        else:
            moveCursor(cursorPos + 1)

    if(modePressed):
        modePressed = False
        openMenu()
        if settings[0][1][settings[0][0]] == "0.5V/div":
            displayFactor = 16
        else:
            displayFactor = 8
        
    if (currentSwitchState == "start" and lastSwitchState == "ready"):
        tft.fillrect((0,9), (160, 128), TFT.BLACK)
        drawAxes()
        cursorPos = 0
        measure()
        if modeButton.value():
            plot()
        
    lastSwitchState = currentSwitchState
    
    if(not lowBatt.value()):
        tft.fill(TFT.BLACK)
        tft.text((48, 60), 'LOW BATTERY', TFT.RED, sysfont, 1, nowrap=True)
        while not lowBatt.value():
            time.sleep(5)
        tft.fill(TFT.BLACK)
        if mode[modeIndex] != "OHM":
            drawAxes()
    
    

