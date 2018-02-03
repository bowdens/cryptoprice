#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <curl/curl.h>

#define PRICELEN 8

char *settings_path = ".cryptoprice_settings";

char *currency = "USD";
char *currency_lower = "usd";
char *defaultcoin = "bitcoin";

int writemode = 0;
const char *writemodes[] = { "humanreadable", "simple" };
const int maxwritemode = 1;

const char *argp_program_bug_address = "@bowdens [github]";
const char *argp_program_version = "cryptoprice 0.1";


void copy_to_lower(char *str, char *tocopy) {
    free(str);
    size_t size = sizeof(tocopy);
    str = malloc(sizeof(char) * size);
    for(int i = 0; i < (int)size; i ++){
        str[i] = tolower(tocopy[i]);
    }
}


int read_settings(char*);
int write_settings(char *);

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t mem_callback(void *contents, size_t size, size_t nmemb, void *userp){
    size_t realsize = size *nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct*)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);

    if(mem->memory == NULL) {
        fprintf(stderr,"not enough memory \n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int print_price(char *json, char *coin){
    if(json == NULL) return 1;

    int found_curr = 0;

    char *price_str;

    char needle[128] = "\"price_";
    strncat(needle, currency_lower, 128);
    strncat(needle, "\": \"", 128);
    char *pos = strstr(json, needle);
    if(pos == NULL) {
        found_curr = 0;
        pos = strstr(json, "\"price_usd\": \"");
        if(pos == NULL) {
            return 2;
        }
        fprintf(stderr,"Warning: Currency '%s' not supported. Defaulting to USD.\n",currency);
        pos += strlen("\"price_usd\": \"");
        price_str = strdup(pos);
        price_str[PRICELEN] = '\0';
    } else {
        found_curr = 1;
        pos += strlen(needle);
        price_str = strdup(pos);
        price_str[PRICELEN] = '\0';
    }

    char *symb = coin;
    char needle2[128] = "\"symbol\": \"";
    char *pos2 = strstr(json, needle2);
    if(pos2 != NULL){
        pos2 += strlen(needle2);
        symb = strdup(pos2);
        int found = 0;
        for(int i = 0; i < strlen(symb); i ++){
            if(symb[i] == '"'){
                symb[i] = '\0';
                found = 1;
                break;
            }
        }
        if(found == 0){
            free(symb);
            symb = coin;
        }
    }

    //printf("pos = \"%s\"\n",pos);

    double price = -1;
    if(sscanf(price_str, "%lf", &price) == 0){
        printf("could not find price in sscanf\n");
        free(price_str);
        free(symb);
        return 1;
    }

    switch(writemode){
        case 0:
            //human
            printf("1 %s = %.2lf %s\n",symb, price, found_curr?currency:"USD");
            break;
        case 1:
            //simple
            printf("%lf\n",price);
            break;
    }
    return 0;
}

int get_price(char *coin, char *curr){
    //api.coinmarketcap.com/v1/ticker/coin/?convert=CUR
    char url[256] = "http://api.coinmarketcap.com/v1/ticker/";
    strcat(url, coin);
    strcat(url, "/?convert=");
    strcat(url, curr);

    CURL *curl;
    CURLcode res;

    struct MemoryStruct chunk;

    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);

    curl = curl_easy_init();
    if(!curl){
        printf("ERROR: Error initialising curl handle\n");
    }

    if((res = curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK)) {
        fprintf(stderr, "error in setopt: %s\n",curl_easy_strerror(res));
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_callback);

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    if((res = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK)) {
        fprintf(stderr,"error in followlocation: %s\n", curl_easy_strerror(res));
    }

    res = curl_easy_perform(curl);

    if(res != CURLE_OK){
        fprintf(stderr, "ERROR: price could not be fetched: %s\n",curl_easy_strerror(res));
    }

    //printf("%lu bytes retreived\n", (long)chunk.size);

    int err = print_price(chunk.memory, coin);
    if(err == 1){
        fprintf(stderr, "There was an error parsing the api response\n");
    }else if(err == 2){
        fprintf(stderr, "Error in api response. Either the resource is not available, or the coin '%s' is not listed\n",coin);
    }

    curl_easy_cleanup(curl);

    free(chunk.memory);

    curl_global_cleanup();

    return 0;
}

void set_writemode(char *_writemode){
    for(int i = 0; i <= maxwritemode; i++) {
        if(strcmp(_writemode, writemodes[i]) == 0){
            writemode = i;
            write_settings(settings_path);
            return;
        }
    }
    fprintf(stderr,"Error: \"%s\" is not a valid writemode\n",_writemode);
    fprintf(stderr,"The valid writemodes are:\n");
    for(int i = 0; i <= maxwritemode; i++){
        printf("%s", writemodes[i]);
        if(i != maxwritemode) printf(", ");
        else printf("\n");
    }
}

static int parse_opt(int key, char *arg, struct argp_state *state) {
    //printf("parse opt called (key = %d). state->arg_num = %d\n",key,state->arg_num);
    switch(key){
        case 'e':
            get_price("ethereum", currency);
            break;
        case 'b':
            get_price("bitcoin", currency);
            break;
        case 'l':
            get_price("litecoin", currency);
            break;
        case 'i':
            get_price("iota", currency);
            break;
        case 'o':
            get_price(arg, currency);
            break;
        case 777:
            free(currency);
            currency = strdup(arg);
            for(int i = 0; i < strlen(currency); i ++){
                currency[i] = toupper(currency[i]);
            }
            copy_to_lower(currency_lower, currency);
            write_settings(settings_path);
            break;
        case 888:
            printf("%s\n",currency);
            break;
        case 999:
            set_writemode(arg);
            break;
        case 1000:
            if(writemode >= 0 && writemode <= maxwritemode){
                printf("Writing in %s mode\n", writemodes[writemode]);
            } else {
                writemode = 0;
                write_settings(settings_path);
                parse_opt(1000, arg, state);
            }
            break;
        case 1001:
            free(defaultcoin);
            defaultcoin = strdup(arg);
            write_settings(settings_path);
            break;
        case 1002:
            printf("%s\n",defaultcoin);
            break;
        case ARGP_KEY_ARG:
            get_price(arg, currency);
            break;
        case ARGP_KEY_NO_ARGS:
            if(arg == NULL) get_price(defaultcoin,currency);
            break;
    };
    return 0;
}

struct argp_option options[] = {
    { 0, 0, 0, 0, "price options" },
    { "eth", 'e', 0, 0, "check price of ethereum" },
    { "btc", 'b', 0, 0, "check price of bitcoin" },
    { "ltc", 'l', 0, 0, "check price of litecoin" },
    { "iota", 'i', 0, 0, "check price of iota" },
    { "coin", 'o', "COIN", 0, "check the price of another coin" },
    { 0, 0, 0, 0, "other options" },
    { "currency", 777, "CURRENCY", 0, "change the default currency" },
    { "cc", 888, 0, 0, "check the current currency" },
    { "writemode", 999, "WRITEMODE", 0, "change the default writemode" },
    { "cwm", 1000, 0, 0, "check the current writemode" },
    { "defaultcoin", 1001, "COIN", 0, "change the default coin (displayed when no arguments are given)" },
    { "cdc", 1002, 0, 0, "check the default coin" },
    { 0 }
};

struct argp argp = {
    options, parse_opt, 0, "gets the price for a number of cryptocurrencies."
};


int main(int argc, char **argv) {
    if(read_settings(settings_path) != 0) {
        fprintf(stderr,"ERROR: There was an error reading the settings file. Aborting\n");
        return 1;
    }
    argp_parse(&argp, argc, argv, 0, 0, 0);

    if(write_settings(settings_path) != 0) {
        fprintf(stderr,"ERROR: There was an error writing the settings\n");
    }
}

int read_settings(char *path) {
    FILE *fp = fopen(path, "r");
    if(fp == NULL) {
        write_settings(path);
        fp = fopen(path, "r");
        if(fp == NULL) return 1;
    }

    char line[1000];
    while(fgets(line, 1000, fp) != NULL) {
        if(strcmp(line, "CURRENCY:\n") == 0) {
            if(fgets(line, 1000, fp) == NULL) {
                fclose(fp);
                return 1;
            } else {
                int len = strlen(line);
                if(line[len-1] == '\n') line[len-1] = '\0';
                currency = strdup(line);
                currency_lower = malloc(sizeof(char) * strlen(currency));
                for(int i = 0; i < strlen(currency); i++){
                    currency_lower[i] = tolower(currency[i]);
                }
                currency_lower[strlen(currency)] = '\0';
            }
        }else if(strcmp(line, "WRITEMODE:\n") == 0) {
            if(fgets(line, 1000, fp) == NULL) {
                fclose(fp);
                return 1;
            } else {
                int temp = 0;
                if(sscanf(line, "%d", &temp) != 1 || temp > maxwritemode || temp < 0){
                    writemode = 0;
                } else {
                    writemode = temp;
                }
            }
        } else if(strcmp(line, "DEFAULTCOIN:\n") == 0) {
            if(fgets(line, 1000, fp) == NULL) {
                fclose(fp);
                return 1;
            } else {
                char temp[1000];
                if(sscanf(line, "%s", temp) != 1){
                    fprintf(stderr, "error reading default coin\n");
                } else {
                    defaultcoin = strdup(temp);
                }
            }
        }
    }
    fclose(fp);
}

int write_settings(char *path) {
    FILE *fp = fopen(path, "w");
    if(fp == NULL) return 1;

    fprintf(fp, "CURRENCY:\n%s\n",currency);
    fprintf(fp, "WRITEMODE:\n%d\n",writemode);
    fprintf(fp, "DEFAULTCOIN:\n%s\n",defaultcoin);
    fclose(fp);
}
