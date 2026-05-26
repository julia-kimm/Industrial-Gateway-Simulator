#include <iostream>
#include <fstream>
#include <random>
#include <thread>
#include <chrono>
#include <string>
#include <csignal>

struct SensorData {
    int temperature;
    int humidity; //(%)
};

static constexpr int kTempMin = 20; //Normal range for temperature
static constexpr int kTempMax = 35; 
static constexpr int kHumidityMin = 40; //Normal range for humidity
static constexpr int kHumidityMax = 70;

static constexpr double kAnomalyProbability = 0.15; // 15% probability of generating an anomaly

static volatile std::sig_atomic_t g_running = 1; // For handling shutdown signal using Ctrl-C
static void handle_sigint(int) { g_running = 0; }


// Generate a SensorData sample (15% probability of being an anomaly) using the random engine.
SensorData generate_sample(std::mt19937& rng) {
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_int_distribution<int> temp_dist(kTempMin, kTempMax);
    std::uniform_int_distribution<int> humi_dist(kHumidityMin, kHumidityMax);

    SensorData data{};

    if (chance(rng) < kAnomalyProbability) { //Generate either temperature or humidity out of normal range
        std::uniform_int_distribution<int> which(0, 1);
        if (which(rng) == 0) {
            // anomaly in temperature: -20 ~ 200 (normal range 20 ~ 35)
            std::uniform_int_distribution<int> bad_temp(-20, 200);
            data.temperature = bad_temp(rng);
            data.humidity = humi_dist(rng);
        } else {
            // anomaly in humidity: -30 ~ 150 (normal range 40 ~ 70)
            std::uniform_int_distribution<int> bad_humi(-30, 150);
            data.temperature = temp_dist(rng);
            data.humidity = bad_humi(rng);
        }
    } else {
        // normal sample within the normal range
        data.temperature = temp_dist(rng);
        data.humidity = humi_dist(rng);
    }
    return data;
}

int main(int argc, char* argv[]) {
    std::string output_file = (argc > 1) ? argv[1] : "../data/sensor_data.txt";
    int interval_sec = (argc > 2) ? std::stoi(argv[2]) : 3;
    // if num_samples == 0, generate infinite samples until Ctrl-C is pressed
    int num_samples = (argc > 3) ? std::stoi(argv[3]) : 0;

    std::signal(SIGINT, handle_sigint);

    // initialize random engine with a real random seed, if available
    std::random_device rd;
    std::mt19937 rng(rd());

    std::cout << "[SENSOR] start  file=" << output_file
              << "interval=" << interval_sec << "s"
              << "samples=" << (num_samples == 0 ? "infinite":std::to_string(num_samples))
              << std::endl;

    int produced = 0;
    while (g_running && (num_samples == 0 || produced < num_samples)) {
        SensorData s = generate_sample(rng);

        //1. append "temp,humidity\n" to the output file
        std::ofstream out(output_file, std::ios::app);
        if (!out) {
            std::cerr << "[SENSOR][ERROR] cannot open output file: " << output_file << std::endl;
            return 1;
        }
        out << s.temperature << "," << s.humidity << "\n";
        out.close();

        //2. print to stdout in the same format
        std::cout << s.temperature << "," << s.humidity << std::endl;

        ++produced;
        if (num_samples != 0 && produced >= num_samples) break;

        // Wait for the specified interval (split into 1-second chunks to respond quickly to shutdown signal)
        for (int i = 0; i < interval_sec && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "[SENSOR] stop  produced=" << produced << " samples" << std::endl;
    return 0;
}
