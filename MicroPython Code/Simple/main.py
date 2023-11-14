from machine import Timer, Pin, PWM, ADC, SPI
import time
import data
from ST7735 import TFT
from sysfont import sysfont

def startTimers():
    global dacTimer, measurementTimer, plottingTimer
    dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=dacHandler)
    measurementTimer = Timer(mode=Timer.PERIODIC, period=10, callback=measurementHandler)
    plottingTimer = Timer(mode=Timer.PERIODIC, period=10, callback=plottingHandler)

def stopTimers():
    global dacTimer, measurementTimer, plottingTimer
    dacTimer.deinit()
    measurementTimer.deinit()
    plottingTimer.deinit()

def dacHandler(timer):
    global dacStep, waveDAC
    if(mode[modeIndex] == "SIN" or mode[modeIndex] == "PWR" or mode[modeIndex] == "PHS"):
        waveDAC.duty_u16(int(sine[dacStep]))
    elif(mode[modeIndex] == "SQR"):
        if (dacStep < 500):
            waveDAC.duty_u16(65535)
        else:
            waveDAC.duty_u16(32768)
    if (dacStep != 999):
        dacStep += 1
    else:
        dacStep = 0

def measurementHandler(timer):
    global cursorPos, voltageDataC1, voltageDataC2, yValsC1, yValsC2, ADCDataC1, ADCDataC2, tft, measuring, currentArrayIndex
    valSumC1 = 0
    valSumC2 = 0
    for y in range(100):
        valSumC1 += C1.read_u16() - C1Ref.read_u16()
        valSumC2 += C2.read_u16() - C2Ref.read_u16()
    ADCDataC1[currentArrayIndex] = valSumC1*0.01
    ADCDataC2[currentArrayIndex] = valSumC2*0.01
    currentArrayIndex += 1
    if (currentArrayIndex == 160):
        currentArrayIndex = 0

def plottingHandler(timer):
    global voltageDataC1, voltageDataC2, yValsC1, yValsC2, ADCDataC1, ADCDataC2, tft, currentArrayIndex, plottingIndex, measuring, cursorPos
    if (plottingIndex == 0 and mode[modeIndex] == "PHS" and not (dacStep > 445 and dacStep < 455)):
        plottingIndex = 0
    elif (plottingIndex < 160):
        cursorPos = plottingIndex
        if(mode[modeIndex] != "PWR"):
            if(currentArrayIndex >= 20):
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex-20])
                voltageDataC2[plottingIndex] = voltageFactor*(ADCDataC2[currentArrayIndex-20])
                yValsC1[plottingIndex] = int(69-displayFactor*voltageDataC1[plottingIndex])
                yValsC2[plottingIndex] = int(69-displayFactor*voltageDataC2[plottingIndex])
                
                tft.pixel((plottingIndex, yValsC2[plottingIndex]),TFT.BLUE)
                tft.pixel((plottingIndex, yValsC1[plottingIndex]),TFT.RED)
            else:
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex+140])
                voltageDataC2[plottingIndex] = voltageFactor*(ADCDataC2[currentArrayIndex+140])
                yValsC1[plottingIndex] = int(69-displayFactor*voltageDataC1[plottingIndex])
                yValsC2[plottingIndex] = int(69-displayFactor*voltageDataC2[plottingIndex])
                
                tft.pixel((plottingIndex, yValsC2[plottingIndex]),TFT.BLUE)
                tft.pixel((plottingIndex, yValsC1[plottingIndex]),TFT.RED)
        else: #power plot mode
            if(currentArrayIndex >= 20):
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex-20])*voltageFactor*(ADCDataC2[currentArrayIndex-20])
                yValsC1[plottingIndex] = int(69-powerDisplayFactor*voltageDataC1[plottingIndex])
                
                tft.pixel((plottingIndex, yValsC1[plottingIndex]),TFT.PURPLE)
            else:
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex+140])*voltageFactor*(ADCDataC2[currentArrayIndex+140])
                yValsC1[plottingIndex] = int(69-powerDisplayFactor*voltageDataC1[plottingIndex])
                
                tft.pixel((plottingIndex, yValsC1[plottingIndex]),TFT.PURPLE)
        plottingIndex+=1
        if(plottingIndex == 160):
            cursorPos = 0
    else:
        measuring=False

def updateText():
    global tft
    tft.text((0, 0), '{:.2f}S'.format(cursorPos*0.01), TFT.GREEN, sysfont, 1, nowrap=True)
    if(mode[modeIndex] != "PWR"):
        tft.text((40, 0), '{:+.2f}V'.format(voltageDataC1[cursorPos]), TFT.RED, sysfont, 1, nowrap=True)
        tft.text((90, 0), '{:+.2f}V'.format(voltageDataC2[cursorPos]), TFT.BLUE, sysfont, 1, nowrap=True)
    else:
        tft.text((40, 0), '{:+.2f}mW'.format(voltageDataC1[cursorPos]), TFT.PURPLE, sysfont, 1, nowrap=True)
    
    tft.text((140, 0), mode[modeIndex], TFT.YELLOW, sysfont, 1, nowrap=True)

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
    if(mode[modeIndex] != "PWR"):
        tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
        tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.RED)
    else:
        tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.PURPLE)
    cursorPos = newPos
    tft.vline((cursorPos, 10), 118, TFT.GREEN)
    if(mode[modeIndex] != "PWR"):
        tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
        tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.RED)
    else:
        tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.PURPLE)

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

voltageFactor = 6.6/65535
displayFactor = 8
powerDisplayFactor = 4

ADCDataC1 = [0] * 160
ADCDataC2 = [0] * 160

voltageDataC1 = [0] * 160
voltageDataC2 = [0] * 160

yValsC1 = [69] * 160
yValsC2 = [69] * 160

currentArrayIndex = 0
plottingIndex = 160
cursorPos = 0

mode = ["SIN", "SQR", "PWR", "PHS"]
modeIndex = 0

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

measuring = False
textUpdateCounter = 0

startTimers()

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
    
    if(not measuring):
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
            tft.fillrect((0,0), (160, 10), TFT.BLACK)
            if(modeIndex < len(mode) - 1):
                modeIndex += 1
            else:
                modeIndex = 0
        
        if (currentSwitchState == "start" and lastSwitchState == "ready"):
            tft.fillrect((0,9), (160, 128), TFT.BLACK)
            drawAxes()
            cursorPos = 0
            plottingIndex = 0
            measuring = True
        lastSwitchState = currentSwitchState

