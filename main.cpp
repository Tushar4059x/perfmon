#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream> // Added this include for istringstream
#include <numeric> // For accumulate
#include <unistd.h> // For sleep()

using namespace std;

vector<long> readCpuStats() {
    vector<long> stats;
    ifstream file("/proc/stat");
    string line;

    if (file.is_open()) {
        getline(file, line); // Read the first line starting with 'cpu'

        istringstream iss(line); // Tokenize the line
        string token;

        // The first token is "cpu", so skip it and read the rest
        getline(iss, token, ' '); // Discard the "cpu" label

        while (iss >> token) { // Extract the rest of the tokens
            stats.push_back(stol(token)); // Convert to long and add to stats
        }

        file.close(); // Don't forget to close the file
    }
    return stats;
}

void calculateCpuUsage(const vector<long>& prevStats, const vector<long>& currStats) {
    if (prevStats.empty() || currStats.empty() || prevStats.size() != currStats.size()) {
        cerr << "Error: Inconsistent or empty CPU stats data." << endl;
        return;
    }

    long totalPrev = accumulate(prevStats.begin(), prevStats.end(), 0L); // Accumulate total time for prev stats
    long totalCurr = accumulate(currStats.begin(), currStats.end(), 0L); // Accumulate total time for curr stats
    long totalDelta = totalCurr - totalPrev;

    if (totalDelta <= 0) {
        cerr << "Error: Invalid totalDelta or data inconsistency." << endl;
        return;
    }

    long idleDelta = currStats[3] - prevStats[3]; // 3rd index is idle time
    double cpuUsage = ((totalDelta - idleDelta) / (double) totalDelta) * 100.0;

    cout << "CPU Usage: " << cpuUsage << "%" << endl;
}

int main() {
    vector<long> prevStats = readCpuStats();

    if (prevStats.empty()) {
        cerr << "Error: Could not read CPU stats." << endl;
        return -1; // Exit with an error code
    }

    while (true) {
        sleep(1); // Update every second

        vector<long> currStats = readCpuStats();

        if (!currStats.empty()) {
            calculateCpuUsage(prevStats, currStats);
            prevStats = currStats; // Update for the next iteration
        } else {
            cerr << "Error: Could not read CPU stats during loop." << endl;
            break; 
        }
    }

    return 0;
}
