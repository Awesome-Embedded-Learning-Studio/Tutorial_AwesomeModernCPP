int read_adc_hw() {
    return 42;
}

struct ADCSensor {
    int read() { return read_adc_hw(); }
};

struct TempSensor {
    int read() { return 25; }
};

template <typename Sensor> int poll(Sensor& sensor) {
    return sensor.read();
}

int poll_adc() {
    ADCSensor adc;
    return poll(adc);
}

int poll_temp() {
    TempSensor temp;
    return poll(temp);
}
