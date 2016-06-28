#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include <edfi/ctl/client.h>

typedef struct {
    const char *long_opt;
    int has_arg;
    int short_opt;
    const char *arg_name;
    const char *desc;
} edfictl_opt_t;

edfictl_opt_t opts[] = {
    {"fault-prob",               required_argument, 'p', "prob",      "The probability of switching to fault execution at every FDP."},
    {"min-fdp-interval",         required_argument, 'f', "num",       "The minimum number of FDPs between two faulty basic blocks."},
    {"min-fault-time-interval",  required_argument, 't', "ms",        "The minimum number of milliseconds between two faulty basic blocks."},
    {"max-time",                 required_argument, 'T', "ms",        "The maximum duration of the experiment in milliseconds."},
    {"max-faults",               required_argument, 'F', "num",       "The maximum number of executed faults."},
    {"rand-seed",                required_argument, 'r', "seed",      "The random seed to use during the experiment."},
    {"dflib-path",               required_argument, 'l', "path",      "The path to the user-provided dynamic fault libary."},
    {"dflib-params",             required_argument, 'L', "params",    "The parameters to pass to the user-provided dynamic fault library."},
};
size_t num_opts = sizeof(opts)/sizeof(edfictl_opt_t);

static void print_usage(char *progname){
    int i;
    char *indent = "  ";
    char buff[1024];

    printf("USAGE: %s [options] <start|stop|stat|test> <server_id>\n", progname);
    printf("OPTIONS:\n");
    for (i=0;i<num_opts;i++) {
    	if (opts[i].has_arg == no_argument) {
    	    sprintf(buff, "%s-%c, --%s", indent, opts[i].short_opt, opts[i].long_opt);
    	}
    	else {
    	    sprintf(buff, "%s-%c, --%s=<%s>", indent, opts[i].short_opt, opts[i].long_opt, opts[i].arg_name);
    	}
        printf("%-50s %s\n", buff, opts[i].desc);
    }
}

static int parse_cmd(const char* cmd_str)
{
    int cmd = -1;
    int i;
    struct {
    	enum edfi_ctl_cmd cmd;
    	char *cmd_str;
    } supported_cmds[__EDFI_CTL_NUM_CMDS] = {
    	{ EDFI_CTL_CMD_STOP,    "stop" },
    	{ EDFI_CTL_CMD_START,   "start" },
    	{ EDFI_CTL_CMD_TEST,    "test" },
    	{ EDFI_CTL_CMD_STAT,    "stat" },
    	{ EDFI_CTL_CMD_UPDATE,  "update" }
    };

    for (i=0;i<__EDFI_CTL_NUM_CMDS;i++) {
    	if (!strcmp(supported_cmds[i].cmd_str, cmd_str)) {
    	    cmd = supported_cmds[i].cmd;
    	    break;
    	}
    }

    return cmd;
}

static int parse_server_id(const char* server_id_str)
{
    char *tailptr = NULL;
    int server_id = strtol(server_id_str, &tailptr, 0);
    if (server_id == 0 && tailptr == server_id_str) {
        server_id = -1;
    }
    return server_id;
}

static void update_cmd_context_data(edfi_cmd_data_t *data, edfi_context_t *context)
{
    if (data->context_size == 0) {
        data->context_size = sizeof(edfi_context_t);
        data->context = context;
    }
}

static void update_cmd_params_data(edfi_cmd_data_t *data, const char *params)
{
    if (data->params_size == 0) {
        data->params_size = strlen(params)+1;
        data->params = (char*) params;
    }
}

static int parse_context_int(const char *opt, const char *arg, edfi_cmd_data_t *data,
    edfi_context_t *context, int *value)
{
    char *tail_ptr = NULL;

    *value = strtol(arg, &tail_ptr, 0);
    if (*value == 0 && arg == tail_ptr) {
    	printf("Error parsing int option --%s with argument %s\n", opt, arg);
    	return -1;
    }
    update_cmd_context_data(data, context);

    return 0;
}

static int parse_context_uint(const char *opt, const char *arg, edfi_cmd_data_t *data,
    edfi_context_t *context, unsigned int *value)
{
    int int_value;
    int ret = parse_context_int(opt, arg, data, context, &int_value);
    if (ret < 0 || int_value < 0) {
    	data->context_size = 0;
        return ret;
    }
    *value = (unsigned int) int_value;
    return 0;
}

static int parse_context_ulonglong(const char *opt, const char *arg, edfi_cmd_data_t *data,
    edfi_context_t *context, unsigned long long *value)
{
    char *tail_ptr = NULL;

    *value = (unsigned long long) strtoll(arg, &tail_ptr, 0);
    if (*value == 0 && arg == tail_ptr) {
    	printf("Error parsing unsigned long long option --%s with argument %s\n", opt, arg);
    	return -1;
    }
    update_cmd_context_data(data, context);

    return 0;
}

static int parse_context_float(const char *opt, const char *arg, edfi_cmd_data_t *data,
    edfi_context_t *context, float *value)
{
    char *tail_ptr = NULL;

    *value = strtof(arg, &tail_ptr);
    if (*value == 0.0 && arg == tail_ptr) {
    	printf("Error parsing unsigned float option --%s with argument %s\n", opt, arg);
    	return -1;
    }
    update_cmd_context_data(data, context);

    return 0;
}

static int parse_context_str(const char *opt, const char *arg, edfi_cmd_data_t *data,
    edfi_context_t *context, char *value, size_t max_len)
{
    if (strlen(arg) == 0 || strlen(arg)+1 > max_len) {
    	printf("Error parsing str option --%s with argument %s (bad length)\n", opt, arg);
    	return -1;
    }
    strcpy(value, arg);
    update_cmd_context_data(data, context);

    return 0;
}

static int parse_params(const char *opt, const char *arg, edfi_cmd_data_t *data)
{
    if (strlen(arg) == 0) {
    	printf("Error parsing str option --%s with argument %s (bad length)\n", opt, arg);
    	return -1;
    }
    update_cmd_params_data(data, arg);

    return 0;
}

static int parse_args(int argc, char **argv, edfi_cmd_data_t *data, int *server_id)
{
    int i, j, c;
    int num_args, ret = 0;
    struct option *getopt_long_opts;
    char* getopt_short_opts;
    static edfi_context_t edfi_context;

    memset(data, 0, sizeof(edfi_cmd_data_t));
    memset(&edfi_context, 0, sizeof(edfi_context_t));

    getopt_long_opts = (struct option*) calloc(num_opts, sizeof(struct option));
    getopt_short_opts = (char*) calloc(num_opts*2, sizeof(char));
    for (i=0, j=0; i<num_opts; i++, j++) {
        getopt_long_opts[i].name = opts[i].long_opt;
        getopt_long_opts[i].has_arg = opts[i].has_arg;
        getopt_long_opts[i].val = opts[i].short_opt;
        getopt_short_opts[j] = opts[i].short_opt;
        if (opts[i].has_arg == required_argument) {
            getopt_short_opts[++j] = ':';
        }
    }

    while (1) {
        int option_index = 0;
        c = getopt_long (argc, argv, getopt_short_opts,
                getopt_long_opts, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 'p':
            ret = parse_context_float(getopt_long_opts[option_index].name, optarg, data, &edfi_context, &edfi_context.c.fault_prob);
	    edfi_context.c.fault_prob_randmax = edfi_context.c.fault_prob * (EDFI_RAND_MAX + 1);
	    if (edfi_context.c.fault_prob > 0 && edfi_context.c.fault_prob_randmax == 0) {
		    edfi_context.c.fault_prob_randmax = 1;
	    }
            break;

        case 'f':
            ret = parse_context_int(getopt_long_opts[option_index].name, optarg, data, &edfi_context, &edfi_context.c.min_fdp_interval);
            break;

        case 't':
            ret = parse_context_uint(getopt_long_opts[option_index].name, optarg, data, &edfi_context, &edfi_context.c.min_fault_time_interval);
            break;

        case 'T':
            ret = parse_context_uint(getopt_long_opts[option_index].name, optarg, data, &edfi_context, &edfi_context.c.max_time);
            break;

        case 'F':
            ret = parse_context_ulonglong(getopt_long_opts[option_index].name, optarg, data, &edfi_context, &edfi_context.c.max_faults);
            break;

        case 'r':
            ret = parse_context_uint(getopt_long_opts[option_index].name, optarg, data, &edfi_context, &edfi_context.c.rand_seed);
            break;

        case 'l':
            ret = parse_context_str(getopt_long_opts[option_index].name, optarg, data, &edfi_context, edfi_context.c.dflib_path, EDFI_DFLIB_PATH_MAX);
            break;

        case 'L':
            ret = parse_params(getopt_long_opts[option_index].name, optarg, data);
            break;

        case '?':
        default:
            ret = -1;
        }

        if (ret < 0) {
            break;
        }
    }
    free(getopt_long_opts);
    free(getopt_short_opts);

    if (ret < 0) {
        return ret;
    }

    /* Parse any remaining command line arguments (not options). */
    num_args = argc - optind;
    if (num_args != 2) {
    	fprintf(stderr, "Insufficient number of arguments!\n");
        return -2;
    }
    if (optind < argc) {
    	ret = parse_cmd(argv[optind]);
    	if (ret < 0) {
    	    return ret;
    	}
    	data->cmd = ret;
    	ret = parse_server_id(argv[optind+1]);
    	if (ret < 0) {
    	    return ret;
    	}
        *server_id = ret;
    }

    return ret;
}

int main(int argc, char **argv)
{
    int server_id;
    edfi_cmd_data_t data;

    if (parse_args(argc, argv, &data, &server_id) < 0) {
    	print_usage(argv[0]);
        exit(1);
    }

    if (edfi_ctl_client_init(server_id) < 0) {
    	perror("edfi_ctl_client_init");
        exit(1);
    }

    if (edfi_ctl_client_send(&data) < 0) {
    	perror("edfi_ctl_client_send");
        exit(1);
    }

    if (edfi_ctl_client_close() < 0) {
    	perror("edfi_ctl_client_close");
        exit(1);
    }

    return 0;
}

