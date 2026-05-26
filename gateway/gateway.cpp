#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <ctime>
#include <csignal>
#include <mosquitto.h>
#include <iomanip>

//Parsed measurement
struct Reading {
    int temperature;
    int humidity;
    double timestamp;
    bool parsed_ok;
};

// normal range for temperature and humidity (for validation)
static constexpr int kTempLow = 0;
static constexpr int kTempHigh = 100;
static constexpr int kHumiLow = 0;
static constexpr int kHumiHigh = 100;

static volatile std::sig_atomic_t g_running = 1;
static void handle_sigint(int) { g_running = 0; }

// return current timestamp as string
static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// Record log on file and console
class Logger {
public:
    explicit Logger(const std::string& path) : out_(path, std::ios::app) {
        if (!out_) {
            std::cerr << "[GATEWAY][FATAL] cannot open log file: " << path << std::endl;
        }
    }
    void info(const std::string& msg)  { write("INFO", msg); }
    void error(const std::string& msg) { write("ERROR", msg); }
private:
    void write(const std::string& level, const std::string& msg) {
        std::string line = "[" + timestamp() + "] [" + level + "] " + msg;
        std::cout << line << std::endl;
        if (out_) { out_ << line << "\n"; out_.flush(); }
    }
    std::ofstream out_;
};

// parse one line of "temp,humidity" and return a Reading struct. If parsing fails, parsed_ok is set to false.
static Reading parse_line(const std::string& line) {
    Reading r{0, 0, 0.0, false};
    std::stringstream ss(line);
    std::string temp_str, humi_str, timestamp_str;
    if (std::getline(ss, temp_str, ',') && std::getline(ss, humi_str, ',') && std::getline(ss, timestamp_str)) {
        try {
            r.temperature = std::stoi(temp_str);
            r.humidity = std::stoi(humi_str);
            r.timestamp = std::stod(timestamp_str);
            r.parsed_ok = true;
        } catch (const std::exception&) {
            r.parsed_ok = false;
        }
    }
    return r;
}

//Validate value of temperature and humidity. If invalid, return false and set reason message.
static bool validate(const Reading& r, std::string& reason) {
    if (r.temperature < kTempLow || r.temperature > kTempHigh) {
        reason = "invalid temperature=" + std::to_string(r.temperature);
        return false;
    }
    if (r.humidity < kHumiLow || r.humidity > kHumiHigh) {
        reason = "invalid humidity=" + std::to_string(r.humidity);
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string sensor_file = (argc > 1) ? argv[1] : "../data/sensor_data.txt";
    std::string log_file = (argc > 2) ? argv[2] : "../logs/gateway.log";
    std::string broker_host = (argc > 3) ? argv[3] : "localhost";
    int broker_port = (argc > 4) ? std::stoi(argv[4]) : 1883;

    std::signal(SIGINT, handle_sigint);
    Logger log(log_file);
    log.info("gateway start");

    // initialize mosquitto library
    mosquitto_lib_init();
    struct mosquitto* mosq = mosquitto_new("cpp-gateway", true, nullptr);
    if (!mosq) {
        log.error("failed to create mosquitto client");
        mosquitto_lib_cleanup();
        return 1;
    }

    int rc = mosquitto_connect(mosq, broker_host.c_str(), broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        log.error("broker connection failed: " + std::string(mosquitto_strerror(rc)) + "(" + broker_host + ":" + std::to_string(broker_port) + ")");
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }
    log.info("connected to MQTT broker " + broker_host + ":" +
             std::to_string(broker_port));
    mosquitto_loop_start(mosq);

    // follow the sensor file
    std::ifstream in(sensor_file);
    if (!in) {
        log.error("cannot open sensor file: " + sensor_file + "(start the sensor first)");
    }
    // ignore existing data, only process new data appended to the file
    if (in) in.seekg(0, std::ios::end);

    int total = 0, valid = 0, invalid = 0;
    while (g_running) {
        std::string line;
        if (in && std::getline(in, line)) {
            if (line.empty()) continue;
            ++total;

            Reading r = parse_line(line);
            if (!r.parsed_ok) {
                ++invalid;
                log.error("parse failed for line: \"" + line + "\"");
                continue;
            }

            log.info("received temp=" + std::to_string(r.temperature) + " humidity=" + std::to_string(r.humidity));

            std::string reason;
            if (!validate(r, reason)) { //validation failed, skip MQTT publish
                ++invalid;
                log.error(reason + " -> dropped (not published)");
                continue;
            }

            // MQTT publish
            std::string temp_payload = std::to_string(r.temperature);
            std::string humi_payload = std::to_string(r.humidity);
            std::ostringstream json;
            json << "{\"temperature\":" << r.temperature << ",\"humidity\":" << r.humidity << ",\"timestamp\":" << std::fixed << std::setprecision(2) << r.timestamp << "}";
            std::string json_payload = json.str();

            int rc1 = mosquitto_publish(mosq, nullptr, "factory/line1/temperature", (int)temp_payload.size(), temp_payload.c_str(), 0, false);
            int rc2 = mosquitto_publish(mosq, nullptr, "factory/line1/humidity", (int)humi_payload.size(), humi_payload.c_str(), 0, false);
            int rc3 = mosquitto_publish(mosq, nullptr, "factory/line1/data", (int)json_payload.size(), json_payload.c_str(), 0, false);

            if (rc1 == MOSQ_ERR_SUCCESS && rc2 == MOSQ_ERR_SUCCESS && rc3 == MOSQ_ERR_SUCCESS) {
                ++valid;
                log.info("MQTT publish success " + json_payload);
            } else {
                log.error("MQTT publish failed (rc=" + std::to_string(rc1) + "," + std::to_string(rc2) + "," + std::to_string(rc3) + ")");
            }
        } else {
            // if EOF is reached, clear the EOF flag and try again after a short delay (file may be appended with new data)
            if (in.eof()) in.clear();
            else if (!in) {
                // if file is still not present, try to open it again
                in.open(sensor_file);
                if (in) {
                    log.info("sensor file appeared, attach");
                    in.seekg(0, std::ios::end);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }

    log.info("gateway shutting down. total=" + std::to_string(total) + " valid=" + std::to_string(valid) + " invalid=" + std::to_string(invalid));

    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}
