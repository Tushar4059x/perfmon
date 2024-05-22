#include <gtk/gtk.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <map>
#include <sys/statvfs.h>
#include <stdexcept>

using namespace std;

GtkWidget *cpu_label, *ram_label, *rx_label, *tx_label, *disk_label, *temp_label;
GtkWidget *cpu_progress, *ram_progress;
GtkWidget *cpu_icon, *ram_icon, *network_icon, *disk_icon, *temp_icon;
GtkWidget *cpu_graph, *ram_graph; 

void readCPUUsage(unsigned long long &total_jiffies, unsigned long long &work_jiffies) {
    ifstream file("/proc/stat");
    if (!file.is_open()) {
        throw runtime_error("Failed to open /proc/stat");
    }

    string line;
    if (!getline(file, line)) {
        file.close();
        throw runtime_error("Failed to read from /proc/stat");
    }

    file.close();

    istringstream iss(line);
    string cpu;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

    if (!(iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal)) {
        throw runtime_error("Failed to parse /proc/stat");
    }

    total_jiffies = user + nice + system + idle + iowait + irq + softirq + steal;
    work_jiffies = user + nice + system + irq + softirq + steal;
}

double calculateCPUUsage() {
    try {
        unsigned long long total_jiffies1, work_jiffies1;
        unsigned long long total_jiffies2, work_jiffies2;

        readCPUUsage(total_jiffies1, work_jiffies1);
        sleep(1);
        readCPUUsage(total_jiffies2, work_jiffies2);

        unsigned long long total_diff = total_jiffies2 - total_jiffies1;
        unsigned long long work_diff = work_jiffies2 - work_jiffies1;

        return (double)work_diff / total_diff * 100.0;
    } catch (const exception &e) {
        cerr << "Error calculating CPU usage: " << e.what() << endl;
        return 0.0;
    }
}

double readRAMUsage() {
    ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        throw runtime_error("Failed to open /proc/meminfo");
    }

    string line;
    unsigned long mem_total = 0, mem_free = 0, buffers = 0, cached = 0;

    while (getline(file, line)) {
        if (line.find("MemTotal") != string::npos)
            sscanf(line.c_str(), "MemTotal: %lu", &mem_total);
        else if (line.find("MemFree") != string::npos)
            sscanf(line.c_str(), "MemFree: %lu", &mem_free);
        else if (line.find("Buffers") != string::npos)
            sscanf(line.c_str(), "Buffers: %lu", &buffers);
        else if (line.find("Cached") != string::npos)
            sscanf(line.c_str(), "Cached: %lu", &cached);
    }

    file.close();

    if (mem_total == 0) {
        throw runtime_error("Invalid memory information");
    }

    double mem_used = mem_total - mem_free - buffers - cached;
    return (mem_used / mem_total) * 100.0;
}

void readNetworkActivity(double &rx_bytes, double &tx_bytes) {
    ifstream file("/proc/net/dev");
    if (!file.is_open()) {
        throw runtime_error("Failed to open /proc/net/dev");
    }

    string line;
    getline(file, line);
    getline(file, line);

    rx_bytes = tx_bytes = 0.0;

    while (getline(file, line)) {
        istringstream iss(line);
        string interface;
        double receive, transmit;

        iss >> interface;
        iss >> receive;
        for (int i = 0; i < 7; i++) {
            iss >> transmit;
        }
        iss >> transmit;
        rx_bytes += receive;
        tx_bytes += transmit;
    }

    file.close();
}

void readDiskActivity(map<string, unsigned long long> &read_bytes,
                      map<string, unsigned long long> &write_bytes) {
    ifstream file("/proc/diskstats");
    if (!file.is_open()) {
        throw runtime_error("Failed to open /proc/diskstats");
    }

    string line;
    read_bytes.clear();
    write_bytes.clear();

    while (getline(file, line)) {
        istringstream iss(line);
        string device;
        unsigned long long reads, read_sectors, writes, write_sectors;

        iss >> device >> device >> device; // skip first three columns
        if (!(iss >> reads >> reads >> read_sectors >> reads >> writes >> writes >> write_sectors >> writes)) {
            continue; // skip lines that cannot be parsed
        }

        if (device.substr(0, 3) == "ram" || device.substr(0, 3) == "loop") {
            continue; // Skip RAM and loopback devices
        }

        read_bytes[device] = read_sectors * 512; // sector size is 512 bytes
        write_bytes[device] = write_sectors * 512;
    }

    file.close();
}

pair<double, double> calculateDiskSpeed() {
    try {
        map<string, unsigned long long> read_bytes1, write_bytes1;
        map<string, unsigned long long> read_bytes2, write_bytes2;

        readDiskActivity(read_bytes1, write_bytes1);
        sleep(1);
        readDiskActivity(read_bytes2, write_bytes2);

        unsigned long long total_read_diff = 0;
        unsigned long long total_write_diff = 0;

        for (const auto &entry : read_bytes1) {
            const string &device = entry.first;
            total_read_diff += (read_bytes2[device] - read_bytes1[device]);
            total_write_diff += (write_bytes2[device] - write_bytes1[device]);
        }

        double read_speed = (double)total_read_diff / 1024; // Convert to KB
        double write_speed = (double)total_write_diff / 1024; // Convert to KB

        return {read_speed, write_speed};
    } catch (const exception &e) {
        cerr << "Error calculating disk speed: " << e.what() << endl;
        return {0.0, 0.0};
    }
}

double readCPUTemperature() {
    ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.is_open()) {
        cerr << "Failed to open /sys/class/thermal/thermal_zone0/temp" << endl;
        return 0.0;
    }

    double temp;
    if (!(file >> temp)) {
        cerr << "Failed to read temperature" << endl;
        file.close();
        return 0.0;
    }

    file.close();
    return temp / 1000.0; // The temperature is usually in millidegree Celsius
}

pair<double, double> readDiskSpaceUsage() {
    ifstream file("/proc/mounts");
    if (!file.is_open()) {
        throw runtime_error("Failed to open /proc/mounts");
    }

    string line;
    double total_space = 0.0;
    double used_space = 0.0;

    while (getline(file, line)) {
        istringstream iss(line);
        string device, mount_point, fs_type;
        iss >> device >> mount_point >> fs_type;

        if (fs_type == "ext4" || fs_type == "xfs" || fs_type == "btrfs" || fs_type == "ntfs" || fs_type == "vfat") {
            struct statvfs buf;
            if (statvfs(mount_point.c_str(), &buf) == 0) {
                total_space += (buf.f_blocks * buf.f_frsize);
                used_space += (buf.f_blocks - buf.f_bfree) * buf.f_frsize;
            }
        }
    }

    file.close();
    return {total_space, used_space};
}

void update_labels() {
    while (true) {
        try {
            double cpu_usage = calculateCPUUsage();
            double ram_usage = readRAMUsage();
            double rx_bytes, tx_bytes;
            readNetworkActivity(rx_bytes, tx_bytes);
            double cpu_temp = readCPUTemperature();
            auto [read_speed, write_speed] = calculateDiskSpeed();

            auto [total_space, used_space] = readDiskSpaceUsage();
            double disk_usage = (used_space / total_space) * 100.0;

            char cpu_text[50];
            char ram_text[50];
            char rx_text[50];
            char tx_text[50];
            char disk_text[50];
            char temp_text[50];

            snprintf(cpu_text, sizeof(cpu_text), "CPU Usage: %.2f%%", cpu_usage);
            snprintf(ram_text, sizeof(ram_text), "RAM Usage: %.2f%%", ram_usage);
            snprintf(rx_text, sizeof(rx_text), "Network RX: %.2f KB", rx_bytes / 1024);
            snprintf(tx_text, sizeof(tx_text), "Network TX: %.2f KB", tx_bytes / 1024);
            snprintf(disk_text, sizeof(disk_text), "Disk Usage: %.2f%%", disk_usage);
            snprintf(temp_text, sizeof(temp_text), "CPU Temp: %.2f°C", cpu_temp);

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_label_set_text(GTK_LABEL(cpu_label), (const gchar *)data);
                return FALSE;
            }, g_strdup(cpu_text));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_label_set_text(GTK_LABEL(ram_label), (const gchar *)data);
                return FALSE;
            }, g_strdup(ram_text));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cpu_progress), *(double *)data);
                return FALSE;
            }, new double(cpu_usage / 100.0));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ram_progress), *(double *)data);
                return FALSE;
            }, new double(ram_usage / 100.0));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_label_set_text(GTK_LABEL(rx_label), (const gchar *)data);
                return FALSE;
            }, g_strdup(rx_text));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_label_set_text(GTK_LABEL(tx_label), (const gchar *)data);
                return FALSE;
            }, g_strdup(tx_text));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_label_set_text(GTK_LABEL(disk_label), (const gchar *)data);
                return FALSE;
            }, g_strdup(disk_text));

            g_idle_add((GSourceFunc)[](gpointer data) -> gboolean {
                gtk_label_set_text(GTK_LABEL(temp_label), (const gchar *)data);
                return FALSE;
            }, g_strdup(temp_text));

        } catch (const exception &e) {
            cerr << "Error updating labels: " << e.what() << endl;
        }

        sleep(1);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "System Monitor");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Add icons
    cpu_icon = gtk_image_new_from_icon_name("cpu-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    ram_icon = gtk_image_new_from_icon_name("memory-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    network_icon = gtk_image_new_from_icon_name("network-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    disk_icon = gtk_image_new_from_icon_name("drive-harddisk-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    temp_icon = gtk_image_new_from_icon_name("temperature-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);

    // Pack icons and labels
    gtk_box_pack_start(GTK_BOX(vbox), cpu_icon, FALSE, FALSE, 0);
    cpu_label = gtk_label_new("CPU Usage: ");
    gtk_box_pack_start(GTK_BOX(vbox), cpu_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), ram_icon, FALSE, FALSE, 0);
    ram_label = gtk_label_new("RAM Usage: ");
    gtk_box_pack_start(GTK_BOX(vbox), ram_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), network_icon, FALSE, FALSE, 0);
    rx_label = gtk_label_new("Network RX: ");
    gtk_box_pack_start(GTK_BOX(vbox), rx_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), network_icon, FALSE, FALSE, 0);
    tx_label = gtk_label_new("Network TX: ");
    gtk_box_pack_start(GTK_BOX(vbox), tx_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), disk_icon, FALSE, FALSE, 0);
    disk_label = gtk_label_new("Disk Usage: ");
    gtk_box_pack_start(GTK_BOX(vbox), disk_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), temp_icon, FALSE, FALSE, 0);
    temp_label = gtk_label_new("CPU Temp: ");
    gtk_box_pack_start(GTK_BOX(vbox), temp_label, FALSE, FALSE, 0);

    cpu_progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), cpu_progress, FALSE, FALSE, 0);

    ram_progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), ram_progress, FALSE, FALSE, 0);

    thread update_thread(update_labels);
    update_thread.detach();

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}