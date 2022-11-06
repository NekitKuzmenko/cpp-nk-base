#include <iostream>
#include <fstream>
#include <thread>
#include <sys/stat.h>

struct {
    char* path = (char*)"./DB"; //path to directory with db files (edit only this param of config)
    char* connector;
    char* statuses;
    char* session_file;
} config;

int session;

unsigned int toPower(int number, int power) {

    unsigned int result = number;

    for(;power > 0; power--) {
        result *= number;
    }

    return result;

}

int main() {
    
    int db_dir_path_length = 0;

    for(int i = 0;;i++) {
        if(config.path[i] != 0) db_dir_path_length++; else break;
    }

    if(config.path[db_dir_path_length-1] != '/') {

        char* path = config.path;

        config.path = new char[db_dir_path_length+1];

        for(int i = 0; i < db_dir_path_length+1; i++) {
            config.path[i] = path[i];
        }

        config.path[db_dir_path_length] = '/';

        db_dir_path_length++;

    }
    
    config.connector = new char[db_dir_path_length+13];
    config.statuses = new char[db_dir_path_length+12];

    char* filenames[] = {(char*)"connector.txt", (char*)"statuses.txt"};

    for(int i = 0 ; i < db_dir_path_length+13; i++) {
        if(i < db_dir_path_length) config.connector[i] = config.path[i]; else config.connector[i] = filenames[0][i-db_dir_path_length];
    }

    for(int i = 0 ; i < db_dir_path_length+12; i++) {
        if(i < db_dir_path_length) config.statuses[i] = config.path[i]; else config.statuses[i] = filenames[1][i-db_dir_path_length];
    }



    char connect_status[3] = {0x01, 0x00, 0x00};
    
    std::fstream connector_file;
    connector_file.open(config.connector, std::ios_base::binary | std::ios_base::out | std::ios_base::in);

    connector_file.write(connect_status, 1);

    for(;;) {

        connector_file.seekg(0);

        char status;

        connector_file.get(status);

        if(status == 0x02) {

            connector_file.get(status);
            session += ((uint8_t)status) * 255;

            connector_file.get(status);
            session += ((uint8_t)status);

            connect_status[0] = 0x00;

            connector_file.clear();
            connector_file.seekp(0, std::ios_base::beg);
            connector_file.write(connect_status, 3);
            connector_file.flush();

            int filename_length = db_dir_path_length+5;

            if(session < 10) filename_length += 1; else if(session < 100) filename_length += 2; else if(session < 1000) filename_length += 3; else if(session < 10000) filename_length += 4; else if(session < 65536) filename_length += 5;
            config.session_file = new char[filename_length];

            for(int i = 0; i < db_dir_path_length; i++) {
                config.session_file[i] = config.path[i];
            }
            
            config.session_file[db_dir_path_length] = 's';
            
            for(int i = 5-(15-filename_length); i > 0; i--) {
                if(i-1 != 0) config.session_file[6+(((db_dir_path_length+10)-(15-filename_length))-i)] = (char)(48 + (session % toPower(10, i-1)) / toPower(10, i-2)); else config.session_file[6+(((db_dir_path_length+10)-(15-filename_length))-i)] = (char)(48 + session % 10);
            }
            
            config.session_file[filename_length-4] = '.';
            config.session_file[filename_length-3] = 't';
            config.session_file[filename_length-2] = 'x';
            config.session_file[filename_length-1] = 't';
            
            std::cout << "\nsession file: " << config.session_file << "\n";

            break;

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }
    
    std::fstream session_file;
    session_file.open(config.session_file, std::ios_base::out);

    session_file.seekp(1);

    char data[] = "hello new database by NK!";

    session_file.write(data, 25);

    session_file.seekp(0);

    char status[1] = {0x01};

    session_file.write(status, 1);

    session_file.close();

    std::fstream statuses_file;
    statuses_file.open(config.statuses, std::ios_base::binary | std::ios_base::out | std::ios_base::in);

    char request_status[1] = {0x02};

    statuses_file.seekp(session);
    statuses_file.write(request_status, 1);
    statuses_file.flush();

    session_file.close();
    session_file.open(config.session_file, std::ios_base::binary | std::ios_base::out | std::ios_base::in);

    for(;;) {

        session_file.seekg(0);

        char response;

        session_file.get(response);
        
        if(response == 0x02) {

            struct stat response_stats;

            stat(config.session_file, &response_stats);
            
            char* answer;

            answer = new char[response_stats.st_size-1];

            for(int i = 1; i < response_stats.st_size; i++) {

                session_file.get(response);

                answer[i-1] = response;

            }

            std::cout << "\nresponse:\n" << answer;

            break;

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }

}