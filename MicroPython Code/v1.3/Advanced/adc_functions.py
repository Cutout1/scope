import time, array, uctypes, rp_devices as devs

def singleSample(adc_channel):
    # Fetch single ADC sample
    ADC_CHAN = adc_channel
    ADC_PIN  = 26 + ADC_CHAN
    
    adc = devs.ADC_DEVICE
    pin = devs.GPIO_PINS[ADC_PIN]
    pad = devs.PAD_PINS[ADC_PIN]
    pin.GPIO_CTRL_REG = devs.GPIO_FUNC_NULL
    pad.PAD_REG = 0
    
    adc.CS_REG = adc.FCS_REG = 0
    adc.CS.EN = 1
    adc.CS.AINSEL = ADC_CHAN
    adc.CS.RROBIN = 0
    adc.CS.START_ONCE = 1
    return adc.RESULT_REG

def multiSample(num_channels, clock_divisor, num_samples):    
    adc = devs.ADC_DEVICE
    
    adc.CS_REG = adc.FCS_REG = 0
    adc.CS.EN = 1
    
    adc.CS.AINSEL = 2
    
    if(num_channels == 1):
        adc.CS.RROBIN = 0 # AINSEL only (channel 2)
    if(num_channels == 2):
        adc.CS.RROBIN = 5 # channels 0 and 2
    if(num_channels == 4):
        adc.CS.RROBIN = 15 # channes 0, 1, 2, 3
    
    DMA_CHAN = 0
    NSAMPLES = 8 + num_samples * num_channels
    dma_chan = devs.DMA_CHANS[DMA_CHAN]
    dma = devs.DMA_DEVICE

    adc.FCS.EN = adc.FCS.DREQ_EN = 1
    adc_buff = array.array('H', (0 for _ in range(NSAMPLES)))
    adc.DIV_REG = clock_divisor << 8
    adc.FCS.THRESH = adc.FCS.OVER = adc.FCS.UNDER = 1

    dma_chan.READ_ADDR_REG = devs.ADC_FIFO_ADDR
    dma_chan.WRITE_ADDR_REG = uctypes.addressof(adc_buff)
    dma_chan.TRANS_COUNT_REG = NSAMPLES

    dma_chan.CTRL_TRIG_REG = 0
    dma_chan.CTRL_TRIG.CHAIN_TO = DMA_CHAN
    dma_chan.CTRL_TRIG.INCR_WRITE = dma_chan.CTRL_TRIG.IRQ_QUIET = 1
    dma_chan.CTRL_TRIG.TREQ_SEL = devs.DREQ_ADC
    dma_chan.CTRL_TRIG.DATA_SIZE = 1
    dma_chan.CTRL_TRIG.EN = 1

    while adc.FCS.LEVEL:
        x = adc.FIFO_REG
        
    adc.CS.START_MANY = 1
    while dma_chan.CTRL_TRIG.BUSY:
        time.sleep_ms(10)
    adc.CS.START_MANY = 0
    dma_chan.CTRL_TRIG.EN = 0
    return adc_buff[8:]
