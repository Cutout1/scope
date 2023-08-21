from machine import Timer, Pin, PWM, ADC, SPI
import time
import data
from ST7735 import TFT
from sysfont import sysfont

def dacHandler(timer):
    global dacStep, sineDAC
    sineDAC.duty_u16(int(sine[dacStep]))
    if (dacStep != 999):
        dacStep += 1
    else:
        dacStep = 0

def measurementHandler(timer):
    global cursorPos, voltageDataC1, voltageDataC2, yValsC1, yValsC2, tft, measuring, start
    if(measuring):
        valSumC1 = 0
        valSumC2 = 0
        for y in range(100):
            valSumC1 += C1.read_u16() - C1Ref.read_u16()
            valSumC2 += C2.read_u16() - C2Ref.read_u16()
        voltageDataC1[cursorPos] = voltageFactor*(valSumC1*0.01)
        voltageDataC2[cursorPos] = voltageFactor*(valSumC2*0.01)
        yValsC1[cursorPos] = int(69-displayFactor*voltageDataC1[cursorPos])
        yValsC2[cursorPos] = int(69-displayFactor*voltageDataC2[cursorPos])
        tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
        tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.RED)
        if(cursorPos == 159):
            start = False
            measuring = False
            moveCursor(0)
        else:
            cursorPos += 1

def updateText():
    global tft
    tft.text((0, 0), '{:.2f}S'.format(cursorPos*0.01), TFT.GREEN, sysfont, 1, nowrap=True)
    tft.text((40, 0), '{:+.2f}V'.format(voltageDataC1[cursorPos]), TFT.RED, sysfont, 1, nowrap=True)
    tft.text((90, 0), '{:+.2f}V'.format(voltageDataC2[cursorPos]), TFT.BLUE, sysfont, 1, nowrap=True)
    tft.text((140, 0), 'SIN', TFT.YELLOW, sysfont, 1, nowrap=True)

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
        tft.pixel((9 + 10*x,68), TFT.YELLOW)
        tft.pixel((9 + 10*x,70), TFT.YELLOW)

def moveCursor(newPos):
    global cursorPos
    tft.vline((cursorPos, 10), 118, TFT.BLACK)
    drawAxes()
    tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
    tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.RED)
    cursorPos = newPos
    tft.vline((cursorPos, 10), 118, TFT.GREEN)
    tft.pixel((cursorPos, yValsC2[cursorPos]),TFT.BLUE)
    tft.pixel((cursorPos, yValsC1[cursorPos]),TFT.RED)

spi = SPI(1, baudrate=133000000, polarity=0, phase=0, sck=Pin(14), mosi=Pin(15), miso=Pin(12))
tft=TFT(spi,10,11,13)
tft.initr()
tft.rgb(True)
tft.rotation(3)
tft.fill(TFT.BLACK)
drawAxes()

sine = data.sineWave()
sinePin = Pin(20, mode=Pin.OUT)
sineDAC = PWM(sinePin)
sineDAC.freq(1000000)
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

voltageDataC1 = [0] * 160
voltageDataC2 = [0] * 160

yValsC1 = [69] * 160
yValsC2 = [69] * 160

cursorPos = 0

leftButton = Pin(0, Pin.IN, Pin.PULL_UP)
rightButton = Pin(1, Pin.IN, Pin.PULL_UP)
modeButton = Pin(2, Pin.IN, Pin.PULL_UP)
selectButton = Pin(3, Pin.IN, Pin.PULL_UP)
startSwitch = Pin(9, Pin.IN, Pin.PULL_UP)
readySwitch = Pin(8, Pin.IN, Pin.PULL_UP)

leftCounter  = 0
rightCounter = 0
modeCounter  = 0
selectButton = 0
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

start = 0

dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=dacHandler)
measurementTimer = Timer(mode=Timer.PERIODIC, period=10, callback=measurementHandler)
measuring = False
textUpdateCounter = 0

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
        
        if(start):
            tft.fillrect((0,9), (160, 128), TFT.BLACK)
            drawAxes()
            cursorPos = 0
            measuring = True
    
    if (currentSwitchState != lastSwitchState):
        if (currentSwitchState == "start" and lastSwitchState == "ready"):
            start = True
    lastSwitchState = currentSwitchState

