#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <time.h>
#include <curl/curl.h>

#define PRICELEN 8

const char *settings_path = ".cryptoprice_settings";
const char *price_path = ".cryptoprice_prices";

char *currency = "USD";
char *currency_lower = "usd";
char *defaultcoin = "bitcoin";

int writemode = 0;
const char *writemodes[] = { "humanreadable", "simple", "change", "24hchange" };
const int maxwritemode = 3;

const char *argp_program_bug_address = "@bowdens [github]";
const char *argp_program_version = "cryptoprice 0.1";

int parsed_args = 0;

int read_settings(const char*);
int write_settings(const char *);

typedef struct _LastPrice {
    char name[1000];
    double price;
    time_t date;
} LastPrice;

LastPrice *read_price(char*, char*);
int write_price(char *coin, char *curr, double price, int date);

void copy_to_lower(char *str, char *tocopy) {
    free(str);
    size_t size = sizeof(tocopy);
    str = malloc(sizeof(char) * size);
    for(int i = 0; i < (int)size; i ++){
        str[i] = tolower(tocopy[i]);
    }
}

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

    double price = -1;
    if(sscanf(price_str, "%lf", &price) == 0){
        fprintf(stderr,"Warning: could not find price in json\n");
        free(price_str);
        free(symb);
        return 1;
    }

    time_t lastdate = 0;
    char needle3[128] = "\"last_updated\": \"";
    char *pos3 = strstr(json, needle3);
    if(pos3 != NULL) {
        pos3 += strlen(needle3);
        char *tempdate = strdup(pos3);
        int found = 0;
        for(int i = 0; i < strlen(tempdate); i++){
            if(tempdate[i] == '"'){
                tempdate[i] = '\0';
                found = 1;
                break;
            }
        }
        if(found == 0){
            free(tempdate);
            lastdate = time(NULL);
        }else{
            if(sscanf(tempdate, "%ld", &lastdate) != 1){
                fprintf(stderr,"Warning: Could not find last_updated in json\n");
                free(tempdate);
                lastdate = time(NULL);
            }
        }
    }

    double change24h = 0;
    char needle4[128] = "\"percent_change_24h\": \"";
    char *pos4 = strstr(json, needle4);
    if(pos4 != NULL) {
        pos4 += strlen(needle4);
        if(sscanf(pos4, "%lf", &change24h) != 1){
            fprintf(stderr, "Warning: Could not find 24h change in json\n");
        }
    }

    char strdatenow[256];
    struct tm *datenow = localtime(&lastdate);
    strftime(strdatenow, 256, "%c", datenow);

    switch(writemode){
        case 0:
            //human
            printf("1 %s = %.2lf %s, as of %s.\n",symb, price, found_curr?currency:"USD",strdatenow);
            break;
        case 1:
            //simple
            printf("%lf\n",price);
            break;
        case 2:
            //change
            {
            printf("1 %s = %.2lf %s, as of %s.", symb, price, found_curr?currency:"USD", strdatenow);

            LastPrice *lp = NULL;
            lp = read_price(symb, found_curr?currency:"USD");
            if(lp){
                double change = price/lp->price * 100.0 - 100;
                if(change < 0) change = -change;
                char strdate[256];
                struct tm *date = localtime(&(lp->date));
                strftime(strdate, 256, "%c", date);
                printf(" %s by %.2lf%% since you last checked on %s\n",
                    lp->price >= price ? "Increased":"Decreased", change, strdate);
            }else{
                printf("\n");
            }
            }
            break;
        case 3:
            //24h change
            printf("1 %s = %.4lf %s %s%.2lf%%\n",symb, price, currency, change24h>=0?" +":" ", change24h);
            break;
    }

    write_price(symb, found_curr?currency:"USD", price, lastdate);

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
        case 'c':
            //set currency
            free(currency);
            currency = strdup(arg);
            for(int i = 0; i < strlen(currency); i ++){
                currency[i] = toupper(currency[i]);
            }
            copy_to_lower(currency_lower, currency);
            write_settings(settings_path);
            //parsed_args++;
            break;
        case 888:
            //check currency
            printf("%s\n",currency);
            parsed_args++;
            break;
        case 'w':
            //set writemode
            set_writemode(arg);
            //parsed_args++;
            break;
        case 1000:
            //check writemode
            if(writemode >= 0 && writemode <= maxwritemode){
                printf("Writing in %s mode\n", writemodes[writemode]);
            } else {
                writemode = 0;
                write_settings(settings_path);
                parse_opt(1000, arg, state);
            }
            parsed_args++;
            break;
        case 'd':
            //set default coin
            free(defaultcoin);
            defaultcoin = strdup(arg);
            write_settings(settings_path);
            //parsed_args++;
            break;
        case 1002:
            //check default coin
            printf("%s\n",defaultcoin);
            parsed_args++;
            break;
        case 1003:
            //purge saved prices
            printf("Warning this will removed all saved price history. Are you sure you want to continue? Y/N\n");
            char line[128];
            scanf("%s", line);
            if(line[0] == 'Y' || line[0] == 'y'){
                int res = remove(price_path);
                if(res == 0){
                    printf("Successfully purged prices\n");
                } else {
                    printf("Could not purge prices. (Error removing file: %d)\n",res);
                }
            } else {
                printf("Did not purge historical prices\n");
            }
            parsed_args++;
            break;
        case ARGP_KEY_ARG:
            get_price(arg, currency);
            parsed_args++;
            break;
        case ARGP_KEY_NO_ARGS:
            if(parsed_args==0) get_price(defaultcoin,currency);
            break;
    }
    return 0;
}

struct argp_option options[] = {
    { 0, 0, 0, 0, "Default Settings", 0 },
    { 0, 0, 0, 0, "Currency", 2 },
    { "currency", 'c', "CURRENCY", 0, "change the default currency" },
    { "cc", 888, 0, 0, "check the current currency" },
    { 0, 0, 0, 0, "Write Mode", 3},
    { "writemode", 'w', "WRITEMODE", 0, "change the default writemode" },
    { "cwm", 1000, 0, 0, "check the current writemode" },
    { 0, 0, 0, 0, "Cryptocurrency", 4},
    { "defaultcoin", 'd', "COIN", 0, "change the default coin (displayed when no arguments are given)" },
    { "cdc", 1002, 0, 0, "check the default coin" },
    { 0, 0, 0, 0, "Other options", -1},
    { "purgeprices", 1003, 0, 0, "deletes all saved historical prices" },
    { 0 }
};

struct argp argp = {
    options, parse_opt, "[CRYTPO...]", "gets the price for a number of cryptocurrencies."
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

int read_settings(const char *path) {
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

int write_settings(const char *path) {
    FILE *fp = fopen(path, "w");
    if(fp == NULL) return 1;

    fprintf(fp, "CURRENCY:\n%s\n",currency);
    fprintf(fp, "WRITEMODE:\n%d\n",writemode);
    fprintf(fp, "DEFAULTCOIN:\n%s\n",defaultcoin);
    fclose(fp);
}

void price_warning(){
    fprintf(stderr, "Warning: '%s' last price file is incorrectly formatted\n",price_path);
}

LastPrice *read_price(char *coin, char *curr){
    FILE *fp = fopen(price_path, "r");
    if(fp == NULL){
        //fprintf(stderr, "Warning: Could not find '%s', so attempting to create file\n",price_path);
        fp = fopen(price_path, "w");
        if(fp == NULL){
            fprintf(stderr, "Warning: Could not create file '%s'\n",price_path);
            return NULL;
        }
        fclose(fp);
        fopen(price_path, "r");
        if(fp == NULL){
            fprintf(stderr, "Warning: Could create file '%s' but could not open it\n",price_path);
            return NULL;
        }
    }

    char line[1000];

    while(fgets(line, 1000, fp) != NULL){
        int size = strlen(line);
        for(int i = size-1; i >= 0; i--){
            if(line[i] == '\n'){
                line[i] = '\0';
                break;
            }
        }
        if(strcmp(line, coin) == 0){
            if(fgets(line, 1000, fp) != NULL){
                int size2 = strlen(line);
                for(int i = size-1; i >= 0; i--){
                    if(line[i] == '\n'){
                        line[i] = '\0';
                        break;
                    }
                }
                if(strcmp(line, curr) == 0){

                    double price = -1;
                    int date = 0;
                    if(fgets(line, 1000, fp) == NULL){
                        price_warning();
                        fclose(fp);
                        return NULL;
                    }
                    if(sscanf(line, "%lf", &price) != 1){
                        price_warning();
                        fclose(fp);
                        return NULL;
                    }
                    if(fgets(line, 1000, fp) == NULL) {
                        price_warning();
                        fclose(fp);
                        return NULL;
                    }
                    if(sscanf(line, "%d", &date) != 1) {
                        price_warning();
                        fclose(fp);
                        return NULL;
                    }
                    LastPrice *lp = malloc(sizeof(LastPrice));
                    strncpy(lp->name, coin, 1000);
                    lp->price = price;
                    lp->date = date;

                    fclose(fp);

                    return lp;
                }
            } else {
                price_warning();
                fclose(fp);
                return NULL;
            }
        }
    }
    fclose(fp);
    //fprintf(stderr, "Could not find matching coin in price history\n");
    return NULL;
}

int write_price(char *coin, char *curr, double price, int date) {
    FILE *fp = fopen(price_path, "r");
    if(fp == NULL){
        fp = fopen(price_path, "w");
        if(fp == NULL) return 1;
        fprintf(fp,"%s\n", coin);
        fprintf(fp,"%s\n", curr);
        fprintf(fp,"%lf\n", price);
        fprintf(fp,"%d\n", date);
        fclose(fp);
        return 0;
    }

    char *tempfile = strdup(price_path);
    strcat(tempfile, ".tmp");
    FILE *temp = fopen(tempfile, "w");

    char *nlcoin = strdup(coin);
    strcat(nlcoin, "\n");
    char *nlcurr = strdup(curr);
    strcat(nlcurr, "\n");

    char line[1000];
    char line2[1000];
    while(fgets(line, 1000, fp) != NULL){
        if(strcmp(line, nlcoin) == 0){
            if(fgets(line2, 1000, fp) != NULL) {
                if(strcmp(line2, nlcurr) == 0){
                    fgets(line, 1000, fp); //price
                    fgets(line, 1000, fp); //date
                } else {
                    fprintf(temp, "%s%s", line, line2);
                }
            }
        } else {
            fprintf(temp, "%s",line);
        }
    }
    fclose(fp);
    remove(price_path);
    free(nlcoin);
    free(nlcurr);

    fprintf(temp, "%s\n%s\n%lf\n%d\n",coin,curr, price, date);
    fclose(temp);

    int res = rename(tempfile, price_path);
    free(tempfile);

    if(res != 0){
        printf("Warning: Could not overwrite old price file\n");
        return 1;
    }
    return 0;
}
