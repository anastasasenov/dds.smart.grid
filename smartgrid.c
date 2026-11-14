/** SmartGrid 1.0 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>
#include "dds/dds.h"
#include "SmartGrid.h"

#define MAX_NODES (256*8)
#define TOPIC_NAME "SmartGrid_NodeStatus"

static volatile bool keep_running = true;

void handle_sigint(int sig) {
    (void)sig;
    keep_running = false;
}

typedef struct {
    SmartGrid_NodeType role;
    char id[65];
    double power;
    int capacity;
    double percentage;
    int switch_time;
    double factor24[24];
} NodeConfig;

/** known node state */
typedef struct {
    SmartGrid_NodeStatus status;
    bool active;
} KnownNode;

static KnownNode grid_nodes[MAX_NODES];
static int grid_node_count = 0;

void print_usage(
    const char *prog_name) {

    printf("Usage: %s [options]\n"
           "\t--consumer <Consumer-id>, The identifier is sorted lexicographically, where the first items have the highest priority.\n"
           "\t--generator <Generator-id>, The identifier is sorted lexicographically, where the first items have the highest priority.\n"
           "\t--battery <Battery-id>, The battery can operate as either a load or a generator, depending on the grid state.\n"
           "\t--logger <Logger-id>, The logger operates in read-only mode, monitoring the state of the grid.\n"
           "\t--power <Watt>, This represents the power of the consumer/generator/battery.\n"
           "\t--capacity <Seconds>, Generator/Consumer/Battery runtime in seconds until fully discharged.\n"
           "\t--percentage <Percent>, Battery percentage of the capacity, default 0%%.\n"
           "\t--switch <Seconds>, Time in seconds required to start up or shut down the generator/consumer/battery, default 1s.\n"
           "\t--factor24 <ListOfPercent>, Hourly solar panel output expressed as a percentage of its maximum power, default 100%%.\n"
           "\t--help Help\n", prog_name);
}

void parse_factor24(
    const char *arg,
    double factor24[24]) {

    char *copy = strdup(arg);
    char *token = strtok(copy, ",");
    int i = 0;
    while (token != NULL && i < 24) {
        factor24[i++] = atof(token);
        token = strtok(NULL, ",");
    }
    /** default 100% */
    double fill = (i > 0) ? factor24[i - 1] : 100.0;
    while (i < 24) {
        factor24[i++] = fill;
    }
    free(copy);
}

bool parse_args(
    int argc,
    char *argv[],
    NodeConfig *cfg) {

    memset(cfg, 0, sizeof(NodeConfig));
    cfg->role = -1;
    cfg->percentage = 0.0;
    cfg->switch_time = 1;
    for (int i = 0; i < 24; i++) cfg->factor24[i] = 100.0;

    static struct option long_options[] = {
        {"consumer",   required_argument, 0, 'c'},
        {"generator",  required_argument, 0, 'g'},
        {"battery",    required_argument, 0, 'b'},
        {"logger",     required_argument, 0, 'l'},
        {"power",      required_argument, 0, 'p'},
        {"capacity",   required_argument, 0, 'k'},
        {"percentage", required_argument, 0, 'r'},
        {"switch",     required_argument, 0, 's'},
        {"factor24",   required_argument, 0, 'f'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:g:b:l:p:k:r:s:f:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                cfg->role = SmartGrid_NODE_CONSUMER;
                strncpy(cfg->id, optarg, sizeof(cfg->id) - 1);
                break;
            case 'g':
                cfg->role = SmartGrid_NODE_GENERATOR;
                strncpy(cfg->id, optarg, sizeof(cfg->id) - 1);
                break;
            case 'b':
                cfg->role = SmartGrid_NODE_BATTERY;
                strncpy(cfg->id, optarg, sizeof(cfg->id) - 1);
                break;
            case 'l':
                cfg->role = SmartGrid_NODE_LOGGER;
                strncpy(cfg->id, optarg, sizeof(cfg->id) - 1);
                break;
            case 'p':
                cfg->power = atof(optarg);
                break;
            case 'k':
                cfg->capacity = atoi(optarg);
                break;
            case 'r':
                cfg->percentage = atof(optarg);
                break;
            case 's':
                cfg->switch_time = atoi(optarg);
                break;
            case 'f':
                parse_factor24(optarg, cfg->factor24);
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return false;
        }
    }

    if ((int)cfg->role == -1 || strlen(cfg->id) == 0) {
        fprintf(stderr, "Error: You must specify a role (--consumer, --generator, --battery, or --logger) with an ID.\n");
        print_usage(argv[0]);
        return false;
    }
    return true;
}

const char* state_to_str(
    SmartGrid_NodeState s) {

    switch (s) {
        case SmartGrid_STATE_OFF:           return "OFF";
        case SmartGrid_STATE_STARTING_UP:   return "STARTING_UP";
        case SmartGrid_STATE_RUNNING:       return "RUNNING";
        case SmartGrid_STATE_CHARGING:      return "CHARGING";
        case SmartGrid_STATE_DISCHARGING:   return "DISCHARGING";
        case SmartGrid_STATE_SHUTTING_DOWN: return "SHUTTING_DOWN";
        case SmartGrid_STATE_DEPLETED:      return "DEPLETED";
        default:                            return "UNKNOWN";
    }
}

const char* type_to_str(
    SmartGrid_NodeType t) {

    switch (t) {
        case SmartGrid_NODE_CONSUMER:  return "CONSUMER";
        case SmartGrid_NODE_GENERATOR: return "GENERATOR";
        case SmartGrid_NODE_BATTERY:   return "BATTERY";
        case SmartGrid_NODE_LOGGER:    return "LOGGER";
        default:                       return "UNKNOWN";
    }
}

int compare_nodes_by_id(
    const void *a,
    const void *b) {

    const KnownNode *na = (const KnownNode *)a;
    const KnownNode *nb = (const KnownNode *)b;
    return strcmp(na->status.id, nb->status.id);
}

/** 
void update_grid_state(
    dds_entity_t reader,
    const char *self_id) {

    void *samples[MAX_NODES];
    dds_sample_info_t infos[MAX_NODES];
    
    for (int i = 0; i < MAX_NODES; i++) samples[i] = NULL;

    int num_samples = dds_take(reader, samples, infos, MAX_NODES, MAX_NODES);
    if (num_samples > 0) {
        for (int i = 0; i < num_samples; i++) {
            SmartGrid_NodeStatus *msg = (SmartGrid_NodeStatus *)samples[i];
            
            int idx = -1;
            for (int j = 0; j < grid_node_count; j++) {
                if (strcmp(grid_nodes[j].status.id, msg->id) == 0) {
                    idx = j;
                    break;
                }
            }

            if (infos[i].valid_data) {
                if (idx == -1 && grid_node_count < MAX_NODES) {
                    idx = grid_node_count++;
                }
                if (idx != -1) {

                    if (self_id == NULL || strcmp(msg->id, self_id) != 0) {
                        grid_nodes[idx].status = *msg;
  
                        strncpy(grid_nodes[idx].status.id, msg->id, sizeof(grid_nodes[idx].status.id) - 1);
                        grid_nodes[idx].status.id[sizeof(grid_nodes[idx].status.id) - 1] = '\0';
                        grid_nodes[idx].active = true;
                    }
                }
            } else if (infos[i].instance_state != DDS_IST_ALIVE && idx != -1) {
                grid_nodes[idx].active = false;
            }
        }
        dds_return_loan(reader, samples, num_samples);
    }

    int active_count = 0;
    for (int i = 0; i < grid_node_count; i++) {
        if (grid_nodes[i].active) {
            if (i != active_count) {
                grid_nodes[active_count] = grid_nodes[i];
            }
            active_count++;
        }
    }
    
    if ( keep_running ) {

        double total_gen = 0.0;
        double total_cons = 0.0;

        printf("\n--- GRID SNAPSHOT (Nodes: %d) ---\n", grid_node_count);
        for (int i = 0; i < grid_node_count; i++) {
            SmartGrid_NodeStatus *n = &grid_nodes[i].status;
            printf("  [%-9s] ID: %-12s | State: %-13s | Power: %7.1f W | RemCap: %4ds | Bat: %5.1f%%\n",
                   type_to_str(n->type), n->id, state_to_str(n->state),
                   n->current_power, n->remaining_capacity_sec, n->battery_percentage);

            if (n->type == SmartGrid_NODE_GENERATOR && n->state == SmartGrid_STATE_RUNNING) {
                total_gen += n->current_power;
            } else if (n->type == SmartGrid_NODE_CONSUMER && n->state == SmartGrid_STATE_RUNNING) {
                total_cons += n->current_power;
            } else if (n->type == SmartGrid_NODE_BATTERY) {
                if (n->state == SmartGrid_STATE_DISCHARGING) total_gen += n->current_power;
                if (n->state == SmartGrid_STATE_CHARGING) total_cons += n->current_power;
            }
        }
        printf("TOTAL GENERATION: %.1f W | TOTAL CONSUMPTION: %.1f W | BALANCE: %+.1f W\n",
               total_gen, total_cons, total_gen - total_cons);

        sleep(1);
    }
}
*/

void update_grid_state(
    dds_entity_t reader,
    const char *self_id) {

    void *samples[MAX_NODES];
    dds_sample_info_t infos[MAX_NODES];
    
    for (int i = 0; i < MAX_NODES; i++) samples[i] = NULL;

    int num_samples = dds_take(reader, samples, infos, MAX_NODES, MAX_NODES);
    if (num_samples > 0) {
        for (int i = 0; i < num_samples; i++) {
            SmartGrid_NodeStatus *msg = (SmartGrid_NodeStatus *)samples[i];
            
            if (self_id != NULL && strcmp(msg->id, self_id) == 0) {
                continue;
            }

            int idx = -1;
            for (int j = 0; j < grid_node_count; j++) {
                if (strcmp(grid_nodes[j].status.id, msg->id) == 0) {
                    idx = j;
                    break;
                }
            }

            if (infos[i].valid_data) {
                if (idx == -1 && grid_node_count < MAX_NODES) {
                    idx = grid_node_count++;
                }
                if (idx != -1) {
                    grid_nodes[idx].status = *msg;
                    strncpy(grid_nodes[idx].status.id, msg->id, sizeof(grid_nodes[idx].status.id) - 1);
                    grid_nodes[idx].status.id[sizeof(grid_nodes[idx].status.id) - 1] = '\0';
                    grid_nodes[idx].active = true;
                }
            } else if (infos[i].instance_state != DDS_IST_ALIVE && idx != -1) {
                grid_nodes[idx].active = false;
            }
        }
        dds_return_loan(reader, samples, num_samples);
    }

    int active_count = 0;
    for (int i = 0; i < grid_node_count; i++) { 
        if (grid_nodes[i].active) {
            if (i != active_count) {
                grid_nodes[active_count] = grid_nodes[i];
            }
            active_count++;
        }
    }
    grid_node_count = active_count;
}

void run_logger(
    dds_entity_t reader,
    const NodeConfig *cfg) {

    printf("[LOGGER %s] Started monitoring in read-only mode...\n", cfg->id);

    while (keep_running) {
        
        /** Fetch latest samples from all active DDS nodes */
        update_grid_state(reader, NULL);

        double total_gen = 0.0;
        double total_cons = 0.0;

        printf("\n--- GRID SNAPSHOT (Active Nodes: %d) ---\n", grid_node_count);
        for (int i = 0; i < grid_node_count; i++) {
            SmartGrid_NodeStatus *n = &grid_nodes[i].status;

            printf("  [%-9s] ID: %-12s | State: %-13s | Power: %7.1f W | RemCap: %4ds | Bat: %5.1f%%\n",
                   type_to_str(n->type),
                   n->id,
                   state_to_str(n->state),
                   n->current_power,
                   n->remaining_capacity_sec,
                   n->battery_percentage);

            /** Accumulate total grid generation and consumption */
            if (n->type == SmartGrid_NODE_GENERATOR && n->state == SmartGrid_STATE_RUNNING) {
                total_gen += n->current_power;
            } else if (n->type == SmartGrid_NODE_CONSUMER && n->state == SmartGrid_STATE_RUNNING) {
                total_cons += n->current_power;
            } else if (n->type == SmartGrid_NODE_BATTERY) {
                if (n->state == SmartGrid_STATE_DISCHARGING) {
                    total_gen += n->current_power;
                } else if (n->state == SmartGrid_STATE_CHARGING) {
                    total_cons += n->current_power;
                }
            }
        }

        printf("TOTAL GENERATION: %.1f W | TOTAL CONSUMPTION: %.1f W | NET BALANCE: %+.1f W\n",
               total_gen, total_cons, total_gen - total_cons);

        sleep(1);
    }
}

void run_active_node(
    dds_entity_t writer,
    dds_entity_t reader,
    const NodeConfig *cfg) {

    SmartGrid_NodeStatus my_status;
    memset(&my_status, 0, sizeof(my_status));
    /** my_status.id = (char *)cfg->id; */
    strncpy(my_status.id, cfg->id, sizeof(my_status.id) - 1);
    my_status.id[sizeof(my_status.id) - 1] = '\0';
    my_status.type = cfg->role;
    my_status.state = SmartGrid_STATE_OFF;
    my_status.nominal_power = cfg->power;
    my_status.total_capacity_sec = cfg->capacity;
    my_status.switch_time_sec = cfg->switch_time;

    if (cfg->role == SmartGrid_NODE_BATTERY) {
        my_status.battery_percentage = cfg->percentage;
        my_status.remaining_capacity_sec = (int)(cfg->capacity * (cfg->percentage / 100.0));
    } else {
        my_status.remaining_capacity_sec = cfg->capacity;
        my_status.battery_percentage = (cfg->capacity > 0) ? 100.0 : 0.0;
    }

    int switch_timer = 0;
    SmartGrid_NodeState target_state_after_switch = SmartGrid_STATE_OFF;
    printf("[%s %s] Initialized. Power: %.1fW, Capacity: %ds, Switch: %ds\n",
    type_to_str(cfg->role), cfg->id, cfg->power, cfg->capacity, cfg->switch_time);
    while (keep_running) {

        /** take solar factor */
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        int hour = tm_info->tm_hour;
        double effective_max_power = cfg->power;
        if (cfg->role == SmartGrid_NODE_GENERATOR) {
            effective_max_power = cfg->power * (cfg->factor24[hour] / 100.0);
        }

        /** read grid nodes */
        update_grid_state(reader, cfg->id);

        /** eval state/prio */
        SmartGrid_NodeState desired_state = SmartGrid_STATE_OFF;
        if (my_status.state == SmartGrid_STATE_DEPLETED) {

            desired_state = SmartGrid_STATE_DEPLETED;

        } else if (cfg->role == SmartGrid_NODE_GENERATOR) { /** generator logic */

            if (cfg->capacity > 0 && my_status.remaining_capacity_sec <= 0) {
                desired_state = SmartGrid_STATE_DEPLETED;
            } else {
                /** consumer sum */
                double total_demand = 0.0;
                double higher_priority_gen = 0.0;
                for (int i = 0; i < grid_node_count; i++) {
                    SmartGrid_NodeStatus *n = &grid_nodes[i].status;
                    /**if (n->type == SmartGrid_NODE_CONSUMER && n->state != SmartGrid_STATE_DEPLETED) {*/
                    if ((n->type == SmartGrid_NODE_CONSUMER || 
                            (n->type == SmartGrid_NODE_BATTERY && n->state == SmartGrid_STATE_CHARGING)) && 
                            n->state != SmartGrid_STATE_DEPLETED) {
                        total_demand += n->nominal_power;
                    }
                    if (n->type == SmartGrid_NODE_GENERATOR && strcmp(n->id, cfg->id) < 0 &&
                        (n->state == SmartGrid_STATE_RUNNING || n->state == SmartGrid_STATE_STARTING_UP)) {
                        higher_priority_gen += n->nominal_power;
                    }
                }
                /** prio generator - switch on */
                if (higher_priority_gen < total_demand && effective_max_power > 0) {
                    desired_state = SmartGrid_STATE_RUNNING;
                } else {
                    desired_state = SmartGrid_STATE_OFF;
                }
            }

        } else if (cfg->role == SmartGrid_NODE_CONSUMER) { /** consumer logic */

            if (cfg->capacity > 0 && my_status.remaining_capacity_sec <= 0) {
                desired_state = SmartGrid_STATE_DEPLETED;
            } else {
                /** generator/battery sum */
                double available_supply = 0.0;
                double higher_priority_demand = 0.0;
                for (int i = 0; i < grid_node_count; i++) {
                    SmartGrid_NodeStatus *n = &grid_nodes[i].status;
                    if (n->type == SmartGrid_NODE_GENERATOR && n->state != SmartGrid_STATE_DEPLETED) {
                        available_supply += (n->current_power > 0) ? n->current_power : n->nominal_power;
                    }
                    if (n->type == SmartGrid_NODE_BATTERY && n->battery_percentage > 0.0) {
                        available_supply += n->nominal_power;
                    }
                    if (n->type == SmartGrid_NODE_CONSUMER && strcmp(n->id, cfg->id) < 0 &&
                        n->state != SmartGrid_STATE_DEPLETED) {
                        higher_priority_demand += n->nominal_power;
                    }
                }

                /** switch on when there are consumers */
                if ((available_supply - higher_priority_demand) >= cfg->power) {
                    desired_state = SmartGrid_STATE_RUNNING;
                } else {
                    desired_state = SmartGrid_STATE_OFF;
                }
            }

        } else if (cfg->role == SmartGrid_NODE_BATTERY) { /** battery logic */

            double total_gen = 0.0;
            double total_cons = 0.0;
            for (int i = 0; i < grid_node_count; i++) {
                SmartGrid_NodeStatus *n = &grid_nodes[i].status;
                if (n->type == SmartGrid_NODE_GENERATOR && n->state == SmartGrid_STATE_RUNNING) {
                    total_gen += n->current_power;
                }
                if (n->type == SmartGrid_NODE_CONSUMER && n->state == SmartGrid_STATE_RUNNING) {
                    total_cons += n->current_power;
                }
            }
            double net_balance = total_gen - total_cons;
            if (net_balance > 0 && my_status.battery_percentage < 100.0) {
                desired_state = SmartGrid_STATE_CHARGING;
            } else if (net_balance < 0 && my_status.battery_percentage > 0.0) {
                desired_state = SmartGrid_STATE_DISCHARGING;
            } else {
                desired_state = SmartGrid_STATE_OFF;
            }
        }

        /** State Machine with --switch time */
        if (desired_state == SmartGrid_STATE_DEPLETED) {
            my_status.state = SmartGrid_STATE_DEPLETED;
            switch_timer = 0;
        } else if (my_status.state == SmartGrid_STATE_STARTING_UP) {
            switch_timer--;
            if (switch_timer <= 0) {
                my_status.state = target_state_after_switch;
            }
        } else if (my_status.state == SmartGrid_STATE_SHUTTING_DOWN) {
            switch_timer--;
            if (switch_timer <= 0) {
                my_status.state = SmartGrid_STATE_OFF;
            }
        } else { /** check for transition to new state */

            bool is_currently_active = (my_status.state == SmartGrid_STATE_RUNNING ||
            my_status.state == SmartGrid_STATE_CHARGING ||
            my_status.state == SmartGrid_STATE_DISCHARGING);
            bool wants_to_be_active = (desired_state == SmartGrid_STATE_RUNNING ||
            desired_state == SmartGrid_STATE_CHARGING ||
            desired_state == SmartGrid_STATE_DISCHARGING);
            if (!is_currently_active && wants_to_be_active) {
                if (cfg->switch_time <= 0) {
                    my_status.state = desired_state;
                } else {
                    my_status.state = SmartGrid_STATE_STARTING_UP;
                    target_state_after_switch = desired_state;
                    switch_timer = cfg->switch_time;
                }
            } else if (is_currently_active && !wants_to_be_active) {
                if (cfg->switch_time <= 0) {
                    my_status.state = SmartGrid_STATE_OFF;
                } else {
                    my_status.state = SmartGrid_STATE_SHUTTING_DOWN;
                    switch_timer = cfg->switch_time;
                }
            } else if (is_currently_active && wants_to_be_active && my_status.state != desired_state) {
                /** battery switch CHARGING / DISCHARGING */
                my_status.state = desired_state;
            }
        }

        /** refresh power/capacity for current second */
        my_status.current_power = 0.0;
        if (my_status.state == SmartGrid_STATE_RUNNING) {
            my_status.current_power = effective_max_power;
            if (cfg->capacity > 0) {
                my_status.remaining_capacity_sec--;
                my_status.battery_percentage = (100.0 * my_status.remaining_capacity_sec) / cfg->capacity;
                if (my_status.remaining_capacity_sec <= 0) {
                    my_status.remaining_capacity_sec = 0;
                    my_status.state = SmartGrid_STATE_DEPLETED;
                    my_status.current_power = 0.0;
                }
            }
        } else if (my_status.state == SmartGrid_STATE_DISCHARGING && cfg->role == SmartGrid_NODE_BATTERY) {
            my_status.current_power = cfg->power;
            my_status.remaining_capacity_sec--;
            if (my_status.remaining_capacity_sec <= 0) {
                my_status.remaining_capacity_sec = 0;
                my_status.battery_percentage = 0.0;
                my_status.state = SmartGrid_STATE_OFF;
                my_status.current_power = 0.0;
            } else {
                my_status.battery_percentage = (100.0 * my_status.remaining_capacity_sec) / cfg->capacity;
            }
        } else if (my_status.state == SmartGrid_STATE_CHARGING && cfg->role == SmartGrid_NODE_BATTERY) {
            my_status.current_power = cfg->power;
            my_status.remaining_capacity_sec++;
            if (my_status.remaining_capacity_sec >= cfg->capacity) {
                my_status.remaining_capacity_sec = cfg->capacity;
                my_status.battery_percentage = 100.0;
                my_status.state = SmartGrid_STATE_OFF;
                my_status.current_power = 0.0;
            } else {
                my_status.battery_percentage = (100.0 * my_status.remaining_capacity_sec) / cfg->capacity;
            }
        }

        my_status.timestamp = (long long)now;

        /** publish state on DDS */
        dds_write(writer, &my_status);
        printf("[%s %s] State: %-13s | Power: %6.1fW | Rem: %4ds (%.1f%%)\n",
        type_to_str(cfg->role), cfg->id, state_to_str(my_status.state),
        my_status.current_power, my_status.remaining_capacity_sec, my_status.battery_percentage);
        sleep(1);
    }

    /** on (Ctrl+C) drop the instance */
    dds_dispose(writer, &my_status);
}

/** entry point */
int main(
    int argc,
    char *argv[]) {

    NodeConfig cfg;
    if (!parse_args(argc, argv, &cfg)) {
        return EXIT_FAILURE;
    }
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    
    /** init Cyclone DDS */
    dds_entity_t participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0) {
        fprintf(stderr, "dds_create_participant failed: %s\n", dds_strretcode(-participant));
        return EXIT_FAILURE;
    }

    /** QoS: RELIABLE & KEEP_LAST(1) */
    dds_qos_t *qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 1);
    dds_entity_t topic = dds_create_topic(participant, &SmartGrid_NodeStatus_desc, TOPIC_NAME, qos, NULL);
    if (topic < 0) {
        fprintf(stderr, "dds_create_topic failed: %s\n", dds_strretcode(-topic));
        return EXIT_FAILURE;
    }

    dds_entity_t reader = dds_create_reader(participant, topic, qos, NULL);
    if (reader < 0) {
        fprintf(stderr, "dds_create_reader failed: %s\n", dds_strretcode(-reader));
        return EXIT_FAILURE;
    }
    
    if (cfg.role == SmartGrid_NODE_LOGGER) {
        run_logger(reader, &cfg); /** read-only logger */
    } else {
        dds_entity_t writer = dds_create_writer(participant, topic, qos, NULL);
        if (writer < 0) {
            fprintf(stderr, "dds_create_writer failed: %s\n", dds_strretcode(-writer));
            return EXIT_FAILURE;
        }
    run_active_node(writer, reader, &cfg);
    }

    dds_delete_qos(qos);
    dds_delete(participant);
    printf("\nNode %s shut down cleanly.\n", cfg.id);

    return EXIT_SUCCESS;
}
