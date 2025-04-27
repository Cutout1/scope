from machine import Timer, Pin, PWM, ADC, SPI, mem32
from array import array
from uctypes import addressof
import time
import data
from ST7735 import TFT
from sysfont import sysfont

machine.freq(192_000_000)

# define DMA register addresses
DMA_BASE = 0x50000000

CH2_READ_ADDR   = DMA_BASE+0x080
CH2_WRITE_ADDR  = DMA_BASE+0x084
CH2_TRANS_COUNT = DMA_BASE+0x088
CH2_CTRL_TRIG   = DMA_BASE+0x08c

CH3_READ_ADDR   = DMA_BASE+0x0c0
CH3_WRITE_ADDR  = DMA_BASE+0x0c4
CH3_TRANS_COUNT = DMA_BASE+0x0c8
CH3_CTRL_TRIG   = DMA_BASE+0x0cc

DMA_TIMER0 = DMA_BASE + 0x420

# define PWM register addresses

PWM_BASE = 0x40050000

CH2_DIV = PWM_BASE + 0x2c
CH2_CC = PWM_BASE + 0x34
CH2_TOP = PWM_BASE + 0x38

def startTimers():
    global measurementTimer, plottingTimer
    #dacTimer = Timer(mode=Timer.PERIODIC, period=1, callback=dacHandler)
    measurementTimer = Timer(mode=Timer.PERIODIC, period=10, callback=measurementHandler)
    plottingTimer = Timer(mode=Timer.PERIODIC, period=10, callback=plottingHandler)

def stopTimers():
    global measurementTimer, plottingTimer
    #dacTimer.deinit()
    measurementTimer.deinit()
    plottingTimer.deinit()

def convertRawToVoltage(u16_val):
    return (u16_val - cal_0_value) * voltageFactor
'''
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
'''
#2-channel chained DMA for DAC output. channel 2 does the transfer, channel 3 reconfigures
p=array('I',[0]) #global 1-element array
def dac_dma(ar,nword):
    #first disable the DMAs to prevent corruption while writing
    mem32[CH2_CTRL_TRIG]=0
    mem32[CH3_CTRL_TRIG]=0
    #setup first DMA which does the actual transfer
    mem32[CH2_READ_ADDR]=addressof(ar)
    mem32[CH2_WRITE_ADDR]=CH2_CC
    mem32[CH2_TRANS_COUNT]=nword
    mem32[DMA_TIMER0] = 0x0001FA00 # divide system clock by 64000 = 3000 samples per second
    IRQ_QUIET=0x1 #do not generate an interrupt
    TREQ_SEL=0x3b #timer 0 triggered
    CHAIN_TO=3    #start channel 3 when done
    RING_SEL=0
    RING_SIZE=0   #no wrapping
    INCR_WRITE=0  #for write to array
    INCR_READ=1   #for read from array
    DATA_SIZE=2   #32-bit word transfer
    HIGH_PRIORITY=1
    EN=1
    CTRL2=(IRQ_QUIET<<21)|(TREQ_SEL<<15)|(CHAIN_TO<<11)|(RING_SEL<<10)|(RING_SIZE<<9)|(INCR_WRITE<<5)|(INCR_READ<<4)|(DATA_SIZE<<2)|(HIGH_PRIORITY<<1)|(EN<<0)
    mem32[CH2_CTRL_TRIG]=CTRL2
    #setup second DMA which reconfigures the first channel
    p[0]=addressof(ar)
    mem32[CH3_READ_ADDR]=addressof(p)
    mem32[CH3_WRITE_ADDR]=CH2_READ_ADDR
    mem32[CH3_TRANS_COUNT]=1
    IRQ_QUIET=0x1 #do not generate an interrupt
    TREQ_SEL=0x3f #no pacing
    CHAIN_TO=2    #start channel 2 when done
    RING_SEL=0
    RING_SIZE=0   #no wrapping
    INCR_WRITE=0  #single write
    INCR_READ=0   #single read
    DATA_SIZE=2   #32-bit word transfer
    HIGH_PRIORITY=1
    EN=1
    CTRL3=(IRQ_QUIET<<21)|(TREQ_SEL<<15)|(CHAIN_TO<<11)|(RING_SEL<<10)|(RING_SIZE<<9)|(INCR_WRITE<<5)|(INCR_READ<<4)|(DATA_SIZE<<2)|(HIGH_PRIORITY<<1)|(EN<<0)
    mem32[CH3_CTRL_TRIG]=CTRL3

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
    skip = False
    if (mode[modeIndex] == "PHS" and plottingIndex == 0 and not (mem32[CH2_TRANS_COUNT] > 1620 and mem32[CH2_TRANS_COUNT] < 1680)):
        skip = True
    if (mode[modeIndex] == "PWR" and plottingIndex == 0):
        if(currentArrayIndex > 1):
            if not (ADCDataC1[currentArrayIndex] < ADCDataC1[currentArrayIndex - 1] and ADCDataC1[currentArrayIndex - 1] > ADCDataC1[currentArrayIndex - 2]):
                skip = True
        else:
            skip = True
    if (plottingIndex < 160 and not skip):
        cursorPos = plottingIndex
        if(mode[modeIndex] != "PWR"):
            if(currentArrayIndex >= 20):
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex-20])
                voltageDataC2[plottingIndex] = voltageFactor*(ADCDataC2[currentArrayIndex-20])
                yValsC1[plottingIndex] = int(69-displayFactor*voltageDataC1[plottingIndex])
                yValsC2[plottingIndex] = int(69-displayFactor*voltageDataC2[plottingIndex])
                
            else:
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex+140])
                voltageDataC2[plottingIndex] = voltageFactor*(ADCDataC2[currentArrayIndex+140])
                yValsC1[plottingIndex] = int(69-displayFactor*voltageDataC1[plottingIndex])
                yValsC2[plottingIndex] = int(69-displayFactor*voltageDataC2[plottingIndex])
                
            if(plottingIndex > 0):
                tft.line((plottingIndex-1, yValsC2[plottingIndex-1]), (plottingIndex, yValsC2[plottingIndex]), TFT.BLUE)
                tft.line((plottingIndex-1, yValsC1[plottingIndex-1]), (plottingIndex, yValsC1[plottingIndex]), TFT.RED)
        else: #power plot mode
            if(currentArrayIndex >= 82):
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex-82])*voltageFactor*(ADCDataC2[currentArrayIndex-82])
                yValsC1[plottingIndex] = int(69-powerDisplayFactor*voltageDataC1[plottingIndex])
            
            else:
                voltageDataC1[plottingIndex] = voltageFactor*(ADCDataC1[currentArrayIndex+78])*voltageFactor*(ADCDataC2[currentArrayIndex+78])
                yValsC1[plottingIndex] = int(69-powerDisplayFactor*voltageDataC1[plottingIndex])
                
            if(plottingIndex > 0):
                tft.line((plottingIndex-1, yValsC1[plottingIndex-1]), (plottingIndex, yValsC1[plottingIndex]), TFT.PURPLE)
        plottingIndex+=1
        if(plottingIndex == 160):
            cursorPos = 0
    elif not skip:
        measuring=False

def updateText():
    global tft, last_c1, last_c2, last_r_text
    if(mode[modeIndex] == "SIN" or mode[modeIndex] == "SQR" or mode[modeIndex] == "PHS"):
        tft.text((0, 0), '{:.2f}S'.format(cursorPos*0.01), TFT.GREEN, sysfont, 1, nowrap=True)
        tft.text((40, 0), '{:+.2f}V'.format(voltageDataC1[cursorPos]), TFT.RED, sysfont, 1, nowrap=True)
        tft.text((90, 0), '{:+.2f}V'.format(voltageDataC2[cursorPos]), TFT.BLUE, sysfont, 1, nowrap=True)
    elif(mode[modeIndex] == "PWR"):
        tft.text((0, 0), '{:.2f}S'.format(cursorPos*0.01), TFT.GREEN, sysfont, 1, nowrap=True)
        tft.text((40, 0), '{:+.2f}mW'.format(voltageDataC1[cursorPos]), TFT.PURPLE, sysfont, 1, nowrap=True)
    elif(mode[modeIndex] == "OHM"):
        r_text = ''
        if not resistorPresent:
            r_text = 'R=N/A     '
        elif ohms < 1000:
            r_text = 'R=' + '{:g}'.format(float('{:.{p}g}'.format(ohms, p=3))) + '       '
        elif ohms < 1000000:
            r_text = 'R=' + '{:g}'.format(float('{:.{p}g}'.format(ohms/1000, p=4))) + 'k      '
        else:
            r_text = 'R=' + '{:g}'.format(float('{:.{p}g}'.format(ohms/1000000, p=4))) + 'M      '
        
        for x in range(10):
            if(last_r_text[x] != r_text[x]):
                tft.text((2 + 18*x, 36), last_r_text[x], TFT.BLACK, sysfont, 3, nowrap=True)
                tft.text((2 + 18*x, 36), r_text[x], TFT.WHITE, sysfont, 3, nowrap=True)
        
        last_r_text = r_text
        #tft.text((12, 10), 'Ohms', TFT.WHITE, sysfont, 1, nowrap=True)
        tft.text((2, 100), 'R is the resistance       between the C1+ pin and   GND, measured in ohms.', TFT.WHITE, sysfont, 1, nowrap=False)
    elif(mode[modeIndex] == "VLT"):
        c1_sum = 0
        c2_sum = 0
        for x in range(1000):
            c1_sum += C1.read_u16() - C1Ref.read_u16()
            c2_sum += C2.read_u16() - C2Ref.read_u16()
        c1_voltage = '{:+.3f}V'.format((c1_sum/1000)*voltageFactor)
        c2_voltage = '{:+.3f}V'.format((c2_sum/1000)*voltageFactor)
        
        tft.text((16, 10), "C1:", TFT.RED, sysfont, 3, nowrap=True)
        tft.text((16, 68), "C2:", TFT.BLUE, sysfont, 3, nowrap=True)
        
        for x in range(7):
            if(last_c1[x] != c1_voltage[x]):
                tft.text((16 + 18*x, 36), last_c1[x], TFT.BLACK, sysfont, 3, nowrap=True)
                tft.text((16 + 18*x, 36), c1_voltage[x], TFT.WHITE, sysfont, 3, nowrap=True)
            if(last_c2[x] != c2_voltage[x]):
                tft.text((16 + 18*x, 94), last_c2[x], TFT.BLACK, sysfont, 3, nowrap=True)
                tft.text((16 + 18*x, 94), c2_voltage[x], TFT.WHITE, sysfont, 3, nowrap=True)
        
        last_c1 = c1_voltage
        last_c2 = c2_voltage
    
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
    if(newPos > 159):
        moveCursor(0)
    elif(newPos < 0):
        moveCursor(159)
    else:
        tft.vline((cursorPos, 10), 118, TFT.BLACK)
        drawAxes()
        if(mode[modeIndex] != "PWR"):
            if(cursorPos > 0):
                tft.line((cursorPos-1, yValsC2[cursorPos-1]), (cursorPos, yValsC2[cursorPos]), TFT.BLUE)
                tft.line((cursorPos-1, yValsC1[cursorPos-1]), (cursorPos, yValsC1[cursorPos]), TFT.RED)
            if(cursorPos < 159):
                tft.line((cursorPos, yValsC2[cursorPos]), (cursorPos+1, yValsC2[cursorPos+1]), TFT.BLUE)
                tft.line((cursorPos, yValsC1[cursorPos]), (cursorPos+1, yValsC1[cursorPos+1]), TFT.RED)
        else:
            if(cursorPos > 0):
                tft.line((cursorPos-1, yValsC1[cursorPos-1]), (cursorPos, yValsC1[cursorPos]), TFT.PURPLE)
            if(cursorPos < 159):
                tft.line((cursorPos-1, yValsC1[cursorPos-1]), (cursorPos, yValsC1[cursorPos]), TFT.PURPLE)
        cursorPos = newPos
        tft.vline((cursorPos, 10), 118, TFT.GREEN)
        if(mode[modeIndex] != "PWR"):
            if(cursorPos > 0):
                tft.line((cursorPos-1, yValsC2[cursorPos-1]), (cursorPos, yValsC2[cursorPos]), TFT.BLUE)
                tft.line((cursorPos-1, yValsC1[cursorPos-1]), (cursorPos, yValsC1[cursorPos]), TFT.RED)
            if(cursorPos < 159):
                tft.line((cursorPos, yValsC2[cursorPos]), (cursorPos+1, yValsC2[cursorPos+1]), TFT.BLUE)
                tft.line((cursorPos, yValsC1[cursorPos]), (cursorPos+1, yValsC1[cursorPos+1]), TFT.RED)
        else:
            if(cursorPos > 0):
                tft.line((cursorPos-1, yValsC1[cursorPos-1]), (cursorPos, yValsC1[cursorPos]), TFT.PURPLE)
            if(cursorPos < 159):
                tft.line((cursorPos-1, yValsC1[cursorPos-1]), (cursorPos, yValsC1[cursorPos]), TFT.PURPLE)

def measureResistance(timer):
    global thev1k, thev10k, thev100k, thev1M, ohms, resistorPresent
    thev1k.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v1k = convertRawToVoltage(val_sum/1000)
    thev1k.value(1)
    thev10k.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v10k = convertRawToVoltage(val_sum/1000)
    thev10k.value(1)
    thev100k.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v100k = convertRawToVoltage(val_sum/1000)
    thev100k.value(1)
    thev1M.value(0)
    time.sleep(0.02)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v1M = convertRawToVoltage(val_sum/1000)
    thev1M.value(1)
    
    num_sources = 0
    resistance_sum = 0
    
    most_accurate = min([v1k, v10k, v100k, v1M], key=lambda x:abs(x-1.65))
    
    if(v1k < 1):
        most_accurate = v1k
    
    if(v1k == most_accurate):
        num_sources += 1
        resistance = (1000 * v1k) / (3.3 - v1k)
        resistance_sum += resistance
    if(v10k == most_accurate):
        num_sources += 1
        resistance = (10000 * v10k) / (3.3 - v10k)
        resistance_sum += resistance
    if(v100k == most_accurate):
        num_sources += 1
        resistance = (100000 * v100k) / (3.3 - v100k)
        resistance_sum += resistance
    if(v1M == most_accurate):
        num_sources += 1
        resistance = (1000000 * v1M) / (3.3 - v1M)
        resistance_sum += resistance
    
    if(abs((1000000 * v1M) / (3.3 - v1M)) > 25000000):
        resistorPresent = False
    else:
        resistorPresent = True
    
    if(num_sources != 0):
        resistance_sum = resistance_sum
        if resistance_sum < 0:
            resistance_sum = 0
        ohms = resistance_sum

def calibrate():
    global cal_0_value, cal_3v3_value
    calibration_file = open("calibration.txt", "w")
    calibration_complete = False
    while not calibration_complete:
        tft.fill(TFT.BLACK)
        tft.text((2, 0), 'GND Calibration: Connect  C1+ directly to GND for   calibration, then press   select', TFT.WHITE, sysfont, 1, nowrap=False)
        while selectButton.value():
            time.sleep(0.1)
        val_sum = 0
        for x in range(1000):
            val_sum += C1.read_u16()
        cal_0_value = val_sum / 1000
        time.sleep(0.5)
        
        tft.fill(TFT.BLACK)
        tft.text((2, 0), '3.3V Calibration: Connect C1+ directly to +3.3 for  calibration, then press   select', TFT.WHITE, sysfont, 1, nowrap=False)
        while selectButton.value():
            time.sleep(0.1)
        val_sum = 0
        for x in range(1000):
            val_sum += C1.read_u16()
        cal_3v3_value = val_sum / 1000
        
        if cal_0_value < 25000 or cal_0_value > 40000 or cal_3v3_value < 55000:
            tft.fill(TFT.BLACK)
            tft.text((2, 0), 'Calibration failed. Pleasetry again.', TFT.WHITE, sysfont, 1, nowrap=False)
            time.sleep(3)
        else:
            calibration_complete = True
    calibration_file.write(str(cal_0_value) + " " + str(cal_3v3_value))
    calibration_file.close()

spi = SPI(1, baudrate=133000000, polarity=0, phase=0, sck=Pin(14), mosi=Pin(15), miso=Pin(12))
tft=TFT(spi,10,11,13)
tft.initr()
tft.rgb(True)
tft.rotation(3)
tft.fill(TFT.BLACK)

sine = data.sine_wave()
wavePin = Pin(20, mode=Pin.OUT)
waveDAC = PWM(wavePin)
waveDAC.freq(100000)
waveDAC.duty_u16(32768)

mem32[CH2_TOP] = 254
mem32[CH2_CC] = 128

C1Ref = ADC(29)
C1 = ADC(28)
C2Ref = ADC(27)
C2 = ADC(26)

leftButton = Pin(0, Pin.IN, Pin.PULL_UP)
rightButton = Pin(1, Pin.IN, Pin.PULL_UP)
modeButton = Pin(2, Pin.IN, Pin.PULL_UP)
selectButton = Pin(3, Pin.IN, Pin.PULL_UP)
startSwitch = Pin(9, Pin.IN, Pin.PULL_UP)
readySwitch = Pin(8, Pin.IN, Pin.PULL_UP)

thev1k = Pin(4, Pin.OUT)
thev1k.value(1)
thev10k = Pin(5, Pin.OUT)
thev10k.value(1)
thev100k = Pin(6, Pin.OUT)
thev100k.value(1)
thev1M = Pin(7, Pin.OUT)
thev1M.value(1)

cal_0_value = 0
cal_3v3_value = 0

time.sleep(0.1)

try:
    # hold down mode while booting to recalibrate
    if not modeButton.value():
        calibrate()
    calibration_file = open("calibration.txt", "r")
    calibration_values = calibration_file.readline().split(" ")
    cal_0_value = float(calibration_values[0])
    cal_3v3_value = float(calibration_values[1])
    calibration_file.close()
except:
    # calibration mandatory if there is no existing calibration.txt
    calibrate()

voltageFactor = 3.3/(cal_3v3_value - cal_0_value)
    
tft.fill(TFT.BLACK)
drawAxes()

c1gnd = Pin(25, Pin.OUT)
c1gnd.value(0)
c2gnd = Pin(24, Pin.OUT)
c2gnd.value(0)
c3gnd = Pin(23, Pin.OUT)
c3gnd.value(0)
c4gnd = Pin(22, Pin.OUT)
c4gnd.value(0)

displayFactor = 16
powerDisplayFactor = 4

ADCDataC1 = [0] * 160
ADCDataC2 = [0] * 160

voltageDataC1 = [0] * 160
voltageDataC2 = [0] * 160

yValsC1 = [69] * 160
yValsC2 = [69] * 160

ohms = 0
resistorPresent = False

currentArrayIndex = 0
plottingIndex = 160
cursorPos = 0

last_c1 = "       "
last_c2 = "       "

last_r_text = "           "

mode = ["SIN", "SQR", "PHS", "PWR", "VLT", "OHM"]
modeIndex = 0

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

measuring = False
textUpdateCounter = 0

# set up waveform output
sin_array = data.sine_wave()
sin_bytes = bytearray(12000)
sqr_bytes = bytearray(12000)

for x in range(3000):
    sin_bytes[4*x + 3] = 0
    sin_bytes[4*x + 2] = 0
    sin_bytes[4*x + 1] = 0
    sin_bytes[4*x + 0] = sin_array[x]

for x in range(1500):
    sqr_bytes[4*x + 3] = 0
    sqr_bytes[4*x + 2] = 0
    sqr_bytes[4*x + 1] = 0
    sqr_bytes[4*x + 0] = 255
    sqr_bytes[4*x + 6003] = 0
    sqr_bytes[4*x + 6002] = 0
    sqr_bytes[4*x + 6001] = 0
    sqr_bytes[4*x + 6000] = 128

dac_dma(sin_bytes, 3000)
startTimers()

resistanceTimer = 0
voltmeterTimer = 0

fastScroll = False

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
            fastScroll = False
        elif (leftCounter > 100):
            leftCounter -= 5
            leftPressed = True
            fastScroll = True
    else:
        leftCounter = 0
    
    if (not rightButton.value()):
        rightCounter += 1
        if (rightCounter == 5):
            rightPressed = True
            fastScroll = False
        elif (rightCounter > 100):
            rightCounter -= 5
            rightPressed = True
            fastScroll = True
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
            if(fastScroll):
                moveCursor(cursorPos - 5)
            else:
                moveCursor(cursorPos - 1)
        
        if(rightPressed):
            rightPressed = False
            if(fastScroll):
                moveCursor(cursorPos + 5)
            else:
                moveCursor(cursorPos + 1)
        
        if(modePressed):
            modePressed = False
            tft.fillrect((0,0), (160, 10), TFT.BLACK)
            if(modeIndex < len(mode) - 1):
                modeIndex += 1
            else:
                modeIndex = 0
            if(mode[modeIndex] == "SQR"):
                dac_dma(sqr_bytes, 3000)
            if(mode[modeIndex] == "PHS"):
                dac_dma(sin_bytes, 3000)
            if(mode[modeIndex] == "VLT"):
                tft.fillrect((0,9), (160, 128), TFT.BLACK)
                stopTimers()
            if(mode[modeIndex] == "OHM"):
                c2gnd = Pin(24, Pin.IN)
                tft.fillrect((0,9), (160, 128), TFT.BLACK)
                last_c1 = "       "
                last_c2 = "       "
                resistanceTimer = Timer(mode=Timer.PERIODIC, period=500, callback=measureResistance)
            if(mode[modeIndex] == "SIN"):
                c2gnd = Pin(24, Pin.OUT)
                c2gnd.value(0)
                tft.fillrect((0,9), (160, 128), TFT.BLACK)
                drawAxes()
                resistanceTimer.deinit()
                last_r_text = "           "
                startTimers()
        
        if (currentSwitchState == "start" and lastSwitchState == "ready" and mode[modeIndex] != "OHM"):
            tft.fillrect((0,9), (160, 128), TFT.BLACK)
            drawAxes()
            cursorPos = 0
            plottingIndex = 0
            measuring = True
        
        lastSwitchState = currentSwitchState
    
    if(not lowBatt.value()):
        tft.fill(TFT.BLACK)
        tft.text((48, 60), 'LOW BATTERY', TFT.RED, sysfont, 1, nowrap=True)
        while not lowBatt.value():
            time.sleep(5)
        tft.fill(TFT.BLACK)
        if mode[modeIndex] != "OHM":
            drawAxes()

