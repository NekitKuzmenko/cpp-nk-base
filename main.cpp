#include <iostream>
#include <fstream>
#include <thread>
#include <sys/stat.h>

struct {
    char* path = (char*)"./DB"; //path to directory with db files
    unsigned short get_threads_limit = 10;
    char* db;
    char* connector;
    char* statuses;
} config;

/*
    you will increase speed of working, if you decrease intervals(except interval of writing db file to disk, this interval will decrease speep of working, if it will minimized. but this interval shuoldn't be very big, this can make losing data in database's process stoping. recomemded interval — 1000-5000 ms)
*/

int db_dir_path_length;

char* db;
timespec last_db_changing;
timespec last_db_writing;
int db_size;
uint64_t db_changed_from;

int nextSessionID = 1;

char* sessions[65536];

timespec last_connector_changing;
timespec last_statuses_changing;

char* input;
long last_input_writing;
long last_input_reading;
int input_size;

unsigned short getting_active = 0, setting_active = 0, writing_active = 0;

struct set_method_params {
    unsigned short session;
    unsigned short request;
    unsigned char method;
    unsigned short path_length;
    unsigned long value_length;
    char* path;
    char* value;
};

set_method_params set_methods_queue[256];
unsigned char set_methods_queue_active[256];

/*

    <null symbol> = <0x00>

    data structure:
        <part name><null symbol><part type><offset to next part>

    data structure example:
        <part 1><0x00><5><offset to part "part 2"><internal part 1 of "part 1"><0x00><6><offset to internal part 2 of "part 1"><number value of internal part 1 of "part 1"><internal part 2 of "part 1"><0x00><7><offset to end of "part 1", becouse "part 2" is last internal part of "part 1"><string value of "part 2">

    you can make unimit count of structure levels. this data structure is like to json

    part types:
        1: structure with 8-bit offset index(max length of part — 256 bytes)
        2: structure with 16-bit offset index(max length of part — 65536 bytes)
        3: structure with 24-bit offset index(max length of part — 16777216 bytes)
        4: structure with 32-bit offset index(max length of part — 4.3*10^9 bytes)
        5: structure with 64-bit offset index(max length of part — 1.85*10^19 bytes)
        6: number value
        7: string value

    path is full path to part(example: "part1", "part1<0x00>part2") or 0x00 to main catalog of database. in part's name are allowed all ascii symbols except null symbol, which will be automatically deleted from part's name during creating part

    part types:
        0x00: structure with 8-bit offset index
        0x01: structure with 16-bit offset index
        0x02: structure with 24-bit offset index
        0x03: structure with 32-bit offset index
        0x04: structure with 64-bit offset index
        0x05: number
        0x06: string

    in request every method must be in structure: <method><method params length><method params>
    for doing a lot methods in one request you need to write methods one after one: <method 1><method 1 params length><method 1 params><method 2><method 2 params length><method 2 params>

    method params length is 16-bit integer

    methods list

    methodID: method name
        request structure
        response structure

    1: check for exists and part type
        <path>
        <0(doesn't exist) || 1(exists)><part type(equal methodID: 2(structure with 8-bit offset index, etc..))>

    2: create structure part with 8-bit offset index(max length of one internal part: 256 bytes)
        <path with part's name to create(example: "part1<0x00>part2<0x00>new_part" where "part1<0x00>part2<0x00>" is path to creating and "new_part" is name of new part, "<0x00>new_part" where "<0x00>" means main catalog of database and "new_part" is name of new part)>
        <0(success) || 1(last part(not new part's name) in path doesn't exist)>

    3: create part with number value
        <path with part's name to create(example: "part1<0x00>part2<0x00>new_part" where "part1<0x00>part2<0x00>" is path to creating and "new_part" is name of new part, "<0x00>new_part" where "<0x00>" means main catalog of database and "new_part" is name of new part)>
        <0(success) || 1(last part(not new part's name) in path doesn't exist)>

    4: create part with string value
        <path with part's name to create(example: "part1<0x00>part2<0x00>new_part" where "part1<0x00>part2<0x00>" is path to creating and "new_part" is name of new part, "<0x00>new_part" where "<0x00>" means main catalog of database and "new_part" is name of new part)>
        <0(success) || 1(last part(not new part's name) in path doesn't exist)>

    5: delete part
        <path with part's name to delete(example: "part1<0x00>part2<0x00>part_to_delete" where "part1<0x00>part2<0x00>" is path to part and "part_to_delete" is name of part for deleting, "<0x00>part_to_delete" where "<0x00>" means main catalog of database and "part_to_delete" is name of part for deleting)>
        <0(success) || 1(part doesn't exist)>

    6: get all
        <full path to structure part>
        <0(success) || 1(part doesn't exist)><names of internal parts, splited by null symbol>

    7: get
        <full path to part with number or string value>
        <0(success) || 1(part doesn't exist) || 2(this is structure part)><value length><number or string value, number value must be formed byte by byte depends on value length>
    
    8: get many
        <path length><full path to part with internal parts with number or string value><parts list's length(16-bit integer)>
        <0(success) || 1(part doesn't exist)><all requested parts

    9: get from all
        <path length><full path to part with internal structure parts><part length><part with number or string value, which will be gotten from all parts in specified in first param path>
        <0(success) || 1(part doesn't exist) || 2(this structure part haven't internal parts with number or string value)><1th part name><>
    
    10: get many from all
        <path length><full path to part with internal structure parts><parts length><parts with number or string value, splited by null symbol, which will be gotten from all parts in specified in first param path>
    
    11: set
        <path length><full path to part with number or string type for setting value><value length><value>
    
    12: add
        <path length><full path to part with number or string type for adding value><value length><value>


    using functions(recomended to client library):
        db.exists(path);
        db.createPart(path, name, type);
        db.deletePart(path, name);
        db.getAll(path);
        db.get(path, name);
        db.getMany(path, names); // names are splited by null symbol or it is array
        db.getFromAll(path, name);
        db.getManyFromAll(path, names); // names are splited by null symbol or it is array
        db.set(path, name, value);
        db.add(path, name, value);

    for making many request in one, using commands:
        db.group();
        db...();
        db...();
        db...();
        db.sendGroup(); // or db.resetGroup() to reset group of requests
    db...() command after calling db.group() will not send requests, but they will add new requests in client's request buffer. after calling db.sendGroup() requests will be sent. db.sendGroup() is blocking function and after receiving response from database this function will return responses in order equals order of requests

    also for convenience are recomended functions db.findPart() and part.findPart(). it will just add path of current part to beginning of requesting path

*/


unsigned int toPower(int number, int power) {

    unsigned int result = number;

    for(;power > 0; power--) {
        result *= number;
    }

    return result;

}


void clients_connector() {

    timespec time;
    struct stat connector_file_stats;

    for(;;) {
        
        if(stat(config.connector, &connector_file_stats) == 0) {
            
            if(connector_file_stats.st_mtim.tv_sec != last_connector_changing.tv_sec || connector_file_stats.st_mtim.tv_nsec != last_connector_changing.tv_nsec) {
                
                last_connector_changing = connector_file_stats.st_mtim;
                
                std::fstream connector_file_status;
                connector_file_status.open(config.connector, std::ios_base::in);

                char status;

                connector_file_status.get(status);
                connector_file_status.close();

                if(status == 0x01) {

                    if(nextSessionID == 65535) {

                        std::cout << "\nlimit of sessons — 65535";

                        exit(0);

                    }

                    std::fstream connector_file;

                    connector_file.open(config.connector, std::ios_base::binary | std::ios_base::out | std::ios_base::in);

                    char sessionID[2];
                    
                    sessionID[0] = nextSessionID / 255;
	                sessionID[1] = nextSessionID % 255;

                    connector_file.seekp(1);

                    connector_file.write(sessionID, 2);

                    connector_file.seekp(0);

                    char connector_status[] = {0x02};

                    connector_file.write(connector_status, 1);

                    connector_file.close();
    
                    clock_gettime(CLOCK_REALTIME, &time);

                    last_connector_changing = time;

                    int filename_length = db_dir_path_length+5;

                    if(nextSessionID < 10) filename_length += 1; else if(nextSessionID < 100) filename_length += 2; else if(nextSessionID < 1000) filename_length += 3; else if(nextSessionID < 10000) filename_length += 4; else if(nextSessionID < 65536) filename_length += 5;
                    
                    sessions[nextSessionID] = new char[filename_length];

                    for(int i = 0; i < db_dir_path_length; i++) {
                        sessions[nextSessionID][i] = config.path[i];
                    }
            
                    sessions[nextSessionID][db_dir_path_length] = 's';
            
                    for(int i = 5-(15-filename_length); i > 0; i--) {
                        if(i-1 != 0) sessions[nextSessionID][6+((5-((db_dir_path_length+10)-filename_length))-i)] = (char)(48 + (nextSessionID % toPower(10, i-1)) / toPower(10, i-2)); else sessions[nextSessionID][6+(((db_dir_path_length+10)-(15-filename_length))-i)] = (char)(48 + nextSessionID % 10);
                    }
            
                    sessions[nextSessionID][filename_length-4] = '.';
                    sessions[nextSessionID][filename_length-3] = 't';
                    sessions[nextSessionID][filename_length-2] = 'x';
                    sessions[nextSessionID][filename_length-1] = 't';

                    nextSessionID++;

                }

            }

        } else {

            std::cout << "\nconnector file with path \"" << config.connector << "\"doesn't exists!";

            exit(0);

        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    }

}


void db_file_writer() {
    
    timespec time;

    std::fstream db_file;
    db_file.open(config.db, std::ios_base::out);

    for(;;) {

        if(setting_active == 0 && (last_db_changing.tv_sec != last_db_writing.tv_sec || last_db_changing.tv_nsec != last_db_writing.tv_nsec)) {

            db_file.seekp(0);
            db_file.write(db, db_size);

            clock_gettime(CLOCK_REALTIME, &time);
            last_db_writing = time;

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    }

}


void get_methods(unsigned short session, unsigned short request, unsigned char method, unsigned short path_length, unsigned short name_length, char path[], char* name) {

    if(setting_active > 0) for(;;) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        if(setting_active == 0) break;
    }

    getting_active++;

    if(method == 1) {

    } else if(method == 10) {

    } else if(method == 11) {

    } else if(method == 12) {

    } else if(method == 13) {

    }

    //writing response

    getting_active--;

}


void set_methods() {

    for(;;) {

        for(int i = 0; i < 256; i++) {

            if(getting_active > 0) for(;;) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                if(getting_active == 0) break;
            }

            if(set_methods_queue_active[i] == 1) {

                setting_active = 1;

                if(set_methods_queue[i].method == 2) {

                } else if(set_methods_queue[i].method == 3) {

                } else if(set_methods_queue[i].method == 4) {

                } else if(set_methods_queue[i].method == 5) {

                } else if(set_methods_queue[i].method == 6) {

                } else if(set_methods_queue[i].method == 7) {

                } else if(set_methods_queue[i].method == 8) {

                } else if(set_methods_queue[i].method == 9) {

                } else if(set_methods_queue[i].method == 15) {

                } else if(set_methods_queue[i].method == 16) {

                }

                set_methods_queue_active[i] = 0;

                setting_active = 0;

                //writing response

            }

        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));

    }

}


void input_parser(int session, int input_length, char* input_data) {

    std::cout << "\nnew request!\ndata:\n" << input_data << "\n";

    std::fstream session_file;
    session_file.open(sessions[session], std::ios_base::out);
    
    session_file.seekp(1);

    char answer[] = "hey! it's answer from new database from NK!";

    session_file.write(answer, 43);

    char response_status[1] = {0x02};

    session_file.seekp(0);

    session_file.write(response_status, 1);

    session_file.close();

    int method = input_data[0];
    int params_length = (int)input_data[2] + (int)input_data[1]*255;

    if(params_length+1 == input_length) {

        if(method == 1) {



        }

    } else for(;;) {



    }

}


void requests_listener() {

    timespec time;
    
    std::fstream statuses_file;
    statuses_file.open(config.statuses, std::ios_base::binary | std::ios_base::out | std::ios_base::in);
    char status;

    for(;;) {
        
        struct stat statuses_file_stats;

        if(stat(config.statuses, &statuses_file_stats) == 0) {
            
            if(statuses_file_stats.st_mtim.tv_sec != last_statuses_changing.tv_sec || statuses_file_stats.st_mtim.tv_nsec != last_statuses_changing.tv_nsec) {

                last_statuses_changing = statuses_file_stats.st_mtim;

                statuses_file.seekg(1);

                for(int i = 1; i < nextSessionID; i++) {
                    
                    statuses_file.get(status);
                    
                    if(status == 0x02) {

                        char request_status[1] = {0x01};

                        statuses_file.clear();
                        statuses_file.seekp(i);
                        statuses_file.write(request_status, 1);
                        statuses_file.flush();

                        struct stat session_file_stats;

                        if(stat(sessions[i], &session_file_stats) == 0) {

                            input = new char[session_file_stats.st_size];

                            std::fstream session_file;
                            session_file.open(sessions[i], std::ios_base::in);

                            char sym;

                            for(int i = 0; i < session_file_stats.st_size; i++) {

                                session_file.get(sym);

                                input[i] = sym;

                            }

                            session_file.seekp(i);
                            
                            char session_status[1] = {0x03};

                            session_file.write(session_status, 1);

                            std::thread handler(input_parser, i, session_file_stats.st_size-1, input);

                        }

                    }

                }

            }

        } else {

            std::cout << "\nstatuses file with path \"" << config.statuses << "\"doesn't exists!";

            exit(0);

        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
    }

}




int main() {

    timespec time;
    
    struct stat db_dir_stat;

    if(stat(config.path, &db_dir_stat) != 0) {
        
        std::cout << "\nDirectory with path \"" << config.path << "\" doesn't exists!";

        exit(0);

    }

    db_dir_path_length = 0;

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

    config.db = new char[db_dir_path_length+6];
    config.connector = new char[db_dir_path_length+13];
    config.statuses = new char[db_dir_path_length+12];

    char* filenames[] = {(char*)"db.txt", (char*)"connector.txt", (char*)"statuses.txt"};

    for(int i = 0 ; i < db_dir_path_length+6; i++) {
        if(i < db_dir_path_length) config.db[i] = config.path[i]; else config.db[i] = filenames[0][i-db_dir_path_length];
    }

    for(int i = 0 ; i < db_dir_path_length+13; i++) {
        if(i < db_dir_path_length) config.connector[i] = config.path[i]; else config.connector[i] = filenames[1][i-db_dir_path_length];
    }

    for(int i = 0 ; i < db_dir_path_length+12; i++) {
        if(i < db_dir_path_length) config.statuses[i] = config.path[i]; else config.statuses[i] = filenames[2][i-db_dir_path_length];
    }
    
    char null[1] = {0x00};
    
    std::fstream connector_file;
    connector_file.open(config.connector, std::ios_base::out);

    connector_file.write(null, 1);
    
    clock_gettime(CLOCK_REALTIME, &time);

    last_connector_changing = time;


    std::fstream statuses_file;
    statuses_file.open(config.statuses, std::ios_base::out);

    statuses_file.write(null, 1);
    
    clock_gettime(CLOCK_REALTIME, &time);

    last_statuses_changing = time;




    struct stat db_file_stats;

    if(stat(config.db, &db_file_stats) == 0) {

        last_db_writing = db_file_stats.st_mtim;
        db_size = db_file_stats.st_size;

    } else {

        std::cout << "\nDB file with path \"" << config.db << "\"doesn't exists!";

        exit(0);

    }

    db = new char[db_size];

    std::fstream db_file;
    db_file.open(config.db, std::ios_base::in);

    char sym;

    for(int i = 0; i < db_size; i++) {

        db_file.get(sym);

        db[i] = sym;

    }
    
    db_file.close();
    
    std::thread clientsConnector(clients_connector);

    requests_listener();

}