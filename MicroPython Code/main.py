from machine import Pin, PWM, SPI, ADC
from ST7735 import TFT
from sysfont import sysfont
import time
import math
import data
import _thread

def DACLoop():
    import time
    sine = data.sineWave()
    tri = data.triWave()
    
    sine_pin = Pin(18, mode=Pin.OUT)
    tri_pin = Pin(19, mode=Pin.OUT)
    square_pin = Pin(20, mode=Pin.OUT)

    sineDAC = PWM(sine_pin)
    triDAC = PWM(tri_pin)
    
    sineDAC.freq(1000000)
    triDAC.freq(1000000)
    
    lastTime = time.ticks_us()
    currentTime = time.ticks_us()
    while True:
        square_pin.value(1)
        for x in range(1000):
            while(time.ticks_diff(currentTime, lastTime) < 1000):
                currentTime = time.ticks_us()
            lastTime = currentTime
            sineDAC.duty_u16(int(sine[x]))
            triDAC.duty_u16(int(tri[x]))
            if(x == 500):
                square_pin.value(0)

_thread.start_new_thread(DACLoop, ())

spi = SPI(1, baudrate=62500000, polarity=0, phase=0, sck=Pin(14), mosi=Pin(15), miso=Pin(12))
tft=TFT(spi,10,11,13)
tft.initr()
tft.rgb(True)
tft.rotation(3)
tft.fill(TFT.BLACK)

C1Ref = ADC(29)
C1 = ADC(28)
C2Ref = ADC(27)
C2 = ADC(26)

startButton = Pin(4, Pin.IN, Pin.PULL_UP)

voltageFactor = 6.6/65535
displayFactor = 120/65535

lastTime = time.ticks_us()
currentTime = time.ticks_us()
while True:
    while(startButton.value()):
        time.sleep(0.001)
    tft.fill(TFT.BLACK)
    for x in range(160):
        while(time.ticks_diff(currentTime, lastTime) < 10000):
            currentTime = time.ticks_us()
        lastTime = currentTime
        valSum = 0
        for y in range(10):
            valSum += C1Ref.read_u16()
        val = valSum/10
        #C1Voltage = scalingFactor*(val) - 3.3
        yVal = int(127-displayFactor*val)
        tft.pixel((x, yVal),TFT.RED)

#while True:
    #C1Voltage = voltageFactor*(C1Ref.read_u16()) - 3.3
    #tft.fill(TFT.BLACK)
    #tft.text((0, 0), str(C1Voltage)[0:4], TFT.WHITE, sysfont, 1)
    #time.sleep(0.1)
    

