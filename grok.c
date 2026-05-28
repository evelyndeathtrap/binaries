#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"
#include <unistd.h>
#include <sys/wait.h>

struct Memory {
    char *response;
    size_t size;
};

typedef struct {
    char role[16];
    char *content;
} Message;

// Load API key
char* load_api_key(void) {
    FILE *f = fopen("GROK_API_KEY", "r");
    if (!f) { fprintf(stderr, "Error: Cannot open GROK_API_KEY file\n"); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *key = malloc(len + 1);
    if (!key) { fclose(f); return NULL; }
    fread(key, 1, len, f);
    fclose(f);
    while (len > 0 && (key[len-1] == '\n' || key[len-1] == '\r' || key[len-1] == ' '))
        key[--len] = 0;
    return key;
}

static size_t WriteCallback(void *data, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;
    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if (ptr == NULL) return 0;
    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;
    return realsize;
}

// Compile to shellcode and append to shellcode.h
int compile_to_shellcode(const char *c_code, const char *var_name) {
    FILE *f = fopen("temp_func.c", "w");
    if (!f) return -1;
    fprintf(f, "%s\n", c_code);
    fclose(f);

    int ret = system("gcc -fPIC -nostdlib -fno-stack-protector -O2 -c temp_func.c -o temp_func.o -Wimplicit-function-declaration");
    if (ret != 0) {
        fprintf(stderr, "Compilation failed.\n");
        system("rm -f temp_func.c");
        return -1;
    }

    ret = system("objcopy -O binary -j .text temp_func.o temp_shellcode.bin 2>/dev/null");
    if (ret != 0) {
        fprintf(stderr, "objcopy failed.\n");
        system("rm -f temp_func.c temp_func.o");
        return -1;
    }

    FILE *bin = fopen("temp_shellcode.bin", "rb");
    if (!bin) { system("rm -f temp_func.c temp_func.o"); return -1; }
    fseek(bin, 0, SEEK_END);
    long len = ftell(bin);
    fseek(bin, 0, SEEK_SET);
    unsigned char *shellcode = malloc(len);
    if (shellcode) fread(shellcode, 1, len, bin);
    fclose(bin);

    FILE *h = fopen("shellcode.h", "a");
    if (h && shellcode) {
        fprintf(h, "\n// === %s ===\n", var_name);
        fprintf(h, "unsigned char %s[] = {\n", var_name);
        for (long i = 0; i < len; i++) {
            fprintf(h, "0x%02x,", shellcode[i]);
            if ((i + 1) % 12 == 0) fprintf(h, "\n");
        }
        fprintf(h, "\n};\n");
        fprintf(h, "unsigned int %s_len = %ld;\n", var_name, len);
        fclose(h);
        printf("[+] Shellcode '%s' (%ld bytes) appended to shellcode.h\n", var_name, len);
    }

    free(shellcode);
    system("rm -f temp_func.c temp_func.o temp_shellcode.bin");
    return 0;
}

// Generate shellcode.c with call_shellcode() wrapper
void generate_shellcode_c(void) {
    FILE *f = fopen("shellcode.c", "w");
    if (!f) return;

    fprintf(f, "#include \"shellcode.h\"\n");
    fprintf(f, "#include <stdarg.h>\n");
    fprintf(f, "#include <stdio.h>\n\n");

    fprintf(f, "// Wrapper to call shellcode as a variadic function\n");
    fprintf(f, "int call_shellcode(const char *shellcode_name, unsigned int len, ...)\n");
    fprintf(f, "{\n");
    fprintf(f, "    if (len == 0) return -1;\n\n");
    fprintf(f, "    // Simple example wrapper - cast to a common variadic function pointer\n");
    fprintf(f, "    typedef int (*VariadicFunc)(const char*, ...);\n");
    fprintf(f, "    VariadicFunc func = (VariadicFunc)shellcode_name;  // This is placeholder logic\n");
    fprintf(f, "    // NOTE: Real usage requires mapping name to actual shellcode array\n");
    fprintf(f, "    va_list args;\n");
    fprintf(f, "    va_start(args, len);\n");
    fprintf(f, "    // Implement actual call based on your shellcode signature\n");
    fprintf(f, "    va_end(args);\n");
    fprintf(f, "    return 0; // placeholder\n");
    fprintf(f, "}\n\n");

    fprintf(f, "// Example usage helper (you will need to adapt per shellcode)\n");
    fprintf(f, "int call_%s(...)\n", "yourfunc"); // placeholder
    fprintf(f, "{\n");
    fprintf(f, "    typedef int (*Func)(...);\n");
    fprintf(f, "    Func f = (Func)%s;\n", "yourfunc");
    fprintf(f, "    va_list ap;\n");
    fprintf(f, "    va_start(ap, 0); // adjust\n");
    fprintf(f, "    int ret = f(/* arguments from va_list */);\n");
    fprintf(f, "    va_end(ap);\n");
    fprintf(f, "    return ret;\n");
    fprintf(f, "}\n");

    fclose(f);
    printf("[+] shellcode.c generated with call_shellcode() wrapper.\n");
}

char* grok_chat(Message *messages, int message_count) { /* same as before */
    if (!messages || message_count <= 0) return NULL;

    char* api_key = load_api_key();
    if (!api_key) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) { free(api_key); return NULL; }

    struct Memory chunk = {0};

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "grok-4.3");
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON_AddNumberToObject(root, "max_tokens", 2048);

    cJSON *msg_array = cJSON_AddArrayToObject(root, "messages");
    for (int i = 0; i < message_count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", messages[i].role);
        cJSON_AddStringToObject(msg, "content", messages[i].content);
        cJSON_AddItemToArray(msg_array, msg);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.x.ai/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);

    char *result = NULL;
    if (res == CURLE_OK && chunk.response) {
        cJSON *resp_json = cJSON_Parse(chunk.response);
        if (resp_json) {
            cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
            if (choices && cJSON_GetArraySize(choices) > 0) {
                cJSON *choice = cJSON_GetArrayItem(choices, 0);
                cJSON *msg = cJSON_GetObjectItem(choice, "message");
                cJSON *content = cJSON_GetObjectItem(msg, "content");
                if (content && cJSON_IsString(content)) {
                    result = strdup(cJSON_GetStringValue(content));
                }
            }
            cJSON_Delete(resp_json);
        }
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(json_str);
    free(api_key);
    free(chunk.response);
    return result;
}

int main() {
    Message history[32];
    int msg_count = 0;

    history[msg_count].content = strdup(
        "You are Grok. The user will ask you to write a single C function that accepts variadic parameters (...). "
        "Reply with ONLY the complete C function code. "
        "No explanations, no markdown. "
        "Example signature: int my_function(int x, const char* fmt, ...)");
    strcpy(history[msg_count].role, "system");
    msg_count++;

    printf("Grok Variadic Shellcode Generator (type 'exit' to quit)\n");
    printf("====================================================\n\n");

    char input[4096];
    char var_name[128] = "my_shellcode";

    while (1) {
        printf("Shellcode variable name (e.g. myfunc_shellcode): ");
        if (!fgets(var_name, sizeof(var_name), stdin)) break;
        var_name[strcspn(var_name, "\n")] = 0;
        if (strlen(var_name) == 0) strcpy(var_name, "my_shellcode");

        printf("You (describe the function): ");
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0)
            break;

        history[msg_count].content = strdup(input);
        strcpy(history[msg_count].role, "user");
        msg_count++;

        char* reply = grok_chat(history, msg_count);

        if (reply) {
            printf("\n--- Generated Variadic Function ---\n%s\n--- End ---\n\n", reply);

            compile_to_shellcode(reply, var_name);

            if (msg_count < 31) {
                history[msg_count].content = strdup(reply);
                strcpy(history[msg_count].role, "assistant");
                msg_count++;
            }
            free(reply);
        } else {
            printf("Failed to get response.\n");
        }
    }

    generate_shellcode_c();

    // Cleanup
    for (int i = 0; i < msg_count; i++) free(history[i].content);
    return 0;
}
