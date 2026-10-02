from machine import Timer, Pin, PWM, ADC, SPI
import time
import data
from ST7735 import TFT
from sysfont import sysfont

spi = SPI(1, baudrate=133000000, polarity=0, phase=0, sck=Pin(14), mosi=Pin(15), miso=Pin(12))
tft=TFT(spi,10,11,13)
tft.initr()
tft.rgb(True)
tft.rotation(3)
tft.fill(TFT.BLACK)

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

C1Ref = ADC(29)
C1 = ADC(28)
C2Ref = ADC(27)
C2 = ADC(26)

voltageFactor = 6.6/65535

# set c2gnd to input to reduce error
c2gnd = Pin(24, Pin.IN)

while True:
    thev1k.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v1k = val_sum/1000 * voltageFactor - 3.29
    time.sleep(0.01)
    thev1k.value(1)
    thev10k.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v10k = val_sum/1000 * voltageFactor - 3.29
    time.sleep(0.01)
    thev10k.value(1)
    thev100k.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v100k = val_sum/1000 * voltageFactor - 3.29
    time.sleep(0.01)
    thev100k.value(1)
    thev1M.value(0)
    time.sleep(0.01)
    val_sum = 0
    for x in range(1000):
        val_sum += C1.read_u16()
    v1M = val_sum/1000 * voltageFactor - 3.29
    time.sleep(0.01)
    thev1M.value(0)
    
    num_sources = 0
    resistance_sum = 0
    
    most_accurate = min([v1k, v10k, v100k, v1M], key=lambda x:abs(x-1.65))
    
    r1M = 0
    
    if(v1k == most_accurate):
        num_sources += 1
        resistance = (1000 * v1k) / (3.29 - v1k)
        resistance_sum += resistance
    if(v10k == most_accurate):
        num_sources += 1
        resistance = (10000 * v10k) / (3.29 - v10k)
        resistance_sum += resistance
    if(v100k == most_accurate):
        num_sources += 1
        resistance = (100000 * v100k) / (3.29 - v100k)
        resistance_sum += resistance
    if(v1M == most_accurate):
        num_sources += 1
        resistance = (1000000 * v1M) / (3.29 - v1M)
        r1M = resistance
        resistance_sum += resistance
    
    if(num_sources != 0):
        #tft.fillrect((0,0), (100, 10), TFT.BLACK)
        if(r1M > 30000000):
            tft.text((0, 0), "No resistor detected", TFT.WHITE, sysfont, 1, nowrap=True)
        else:
            tft.text((0, 0), '{:g}'.format(float('{:.{p}g}'.format(resistance_sum, p=3))) + " Ohms                ", TFT.WHITE, sysfont, 1, nowrap=True)
    
    #time.sleep(0.1)
    
    