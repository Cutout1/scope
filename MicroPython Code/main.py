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
    global cursorPos, voltageDataC1, voltageDataC2, yValsC1, yValsC2, tft, measuring
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

def leftHandler(pin):
    global leftPressed, leftDebounce
    if (time.ticks_diff(time.ticks_ms(),leftDebounce)) > 200:
        leftPressed = 1
        leftDebounce = time.ticks_ms()

def rightHandler(pin):
    global rightPressed, rightDebounce
    if (time.ticks_diff(time.ticks_ms(),rightDebounce)) > 200:
        rightPressed = 1
        rightDebounce = time.ticks_ms()
        
def modeHandler(pin):
    global modePressed, modeDebounce, constantPlot
    if (time.ticks_diff(time.ticks_ms(),modeDebounce)) > 200:
        modePressed = 1
        modeDebounce = time.ticks_ms()
        constantPlot = not constantPlot

def selectHandler(pin):
    global selectPressed, selectDebounce, start
    if (time.ticks_diff(time.ticks_ms(),selectDebounce)) > 200:
        selectPressed = 1
        start = True
        selectDebounce = time.ticks_ms()

def startHandler(pin):
    global hasReset, startDebounce, start
    if (time.ticks_diff(time.ticks_ms(),startDebounce)) > 200:
        start = True
        hasReset = 0
        startDebounce = time.ticks_ms()

def resetHandler(pin):
    global hasReset, resetDebounce
    if (time.ticks_diff(time.ticks_ms(),resetDebounce)) > 200:
        hasReset = 1
        resetDebounce = time.ticks_ms()

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

yValsC1 = [0] * 160
yValsC2 = [0] * 160

cursorPos = 0

leftButton = Pin(0, Pin.IN, Pin.PULL_UP)
leftPressed = 0
leftDebounce = 0
leftButton.irq(trigger=Pin.IRQ_FALLING, handler=leftHandler)

rightButton = Pin(1, Pin.IN, Pin.PULL_UP)
rightPressed = 0
rightDebounce = 0
rightButton.irq(trigger=Pin.IRQ_FALLING, handler=rightHandler)

modeButton = Pin(2, Pin.IN, Pin.PULL_UP)
modePressed = 0
modeDebounce = 0
modeButton.irq(trigger=Pin.IRQ_FALLING, handler=modeHandler)

selectButton = Pin(3, Pin.IN, Pin.PULL_UP)
selectPressed = 0
selectDebounce = 0
selectButton.irq(trigger=Pin.IRQ_FALLING, handler=selectHandler)

startSwitch = Pin(9, Pin.IN, Pin.PULL_UP)
startDebounce = 0
startSwitch.irq(trigger=Pin.IRQ_FALLING, handler=startHandler)

readySwitch = Pin(8, Pin.IN, Pin.PULL_UP)
resetDebounce = 0
hasReset = 0
readySwitch.irq(trigger=Pin.IRQ_FALLING, handler=resetHandler)

start = 0

dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=dacHandler)
measurementTimer = Timer(mode=Timer.PERIODIC, period=10, callback=measurementHandler)
measuring = False
resetCounter = 0
textUpdateCounter = 0
constantPlot = False

while(True):
    time.sleep(0.001)
    textUpdateCounter += 1
    if(textUpdateCounter >= 10):
        textUpdateCounter = 0
        updateText()
    if(not measuring):
        if(leftPressed):
            leftPressed = 0
            if(cursorPos == 0):
                moveCursor(159)
            else:
                moveCursor(cursorPos - 1)
        
        if(rightPressed):
            rightPressed = 0
            if(cursorPos == 159):
                moveCursor(0)
            else:
                moveCursor(cursorPos + 1)
        
        if(start or constantPlot):
            tft.fillrect((0,9), (160, 128), TFT.BLACK)
            drawAxes()
            cursorPos = 0
            measuring = True
            start = False
            resetCounter = 0

