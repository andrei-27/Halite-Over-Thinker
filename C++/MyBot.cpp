#include <bits/stdc++.h>
#include <fstream>
#include <vector>

#include "hlt.hpp"
#include "networking.hpp"

#define MAX_STRENGTH 255
#define BOT_NAME "HOT"

#define ALPHA 0.19
#define ENEMY_ROI -1.11
#define EXPLORATION_DELAY 5
#define ATTACK_DELAY 7
#define INF 1e9
#define NAP_ON true

using std::set;
using std::map;
using std::vector;
using std::pair;

static int turn = -1;
static double DELTA_DEGRADATION = 0.45;
static int delay = EXPLORATION_DELAY;
static unsigned char myID;
static bool NAP = false;

static set<hlt::Move> moves;
static std::map<pair<unsigned char, unsigned char>, double> pf_map;
static set<int> nap_allies;
static std::map<pair<unsigned short, unsigned short>, unsigned char> previous_ownership;

inline double degrade_potential(double potential, int distance) {
    return potential + DELTA_DEGRADATION * pow(distance, 2);
}

unsigned char opposite(unsigned char direction) {
    if (direction == NORTH) return SOUTH;
    if (direction == EAST) return WEST;
    if (direction == SOUTH) return NORTH;
    if (direction == WEST) return EAST;
    return STILL;
}

bool has_been_attacked(hlt::GameMap &game_map) {
    for (unsigned short x = 0; x < game_map.width; ++x) {
        for (unsigned short y = 0; y < game_map.height; ++y) {
            hlt::Location loc = {x, y};
            hlt::Site &site = game_map.getSite(loc);

            // Check if a cell owned by us has been taken over
            if (previous_ownership[{x, y}] == myID && site.owner != myID) {
                return true;
            }

            // Check if a neutral cell adjacent to our cells has been taken by an enemy
            if (previous_ownership[{x, y}] == 0 && site.owner != 0 && site.owner != myID) {
                for (const int d : CARDINALS) {
                    hlt::Location neighbor = game_map.getLocation(loc, d);
                    if (game_map.getSite(neighbor).owner == myID) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void update_previous_ownership(hlt::GameMap &game_map) {
    for (unsigned short x = 0; x < game_map.width; ++x) {
        for (unsigned short y = 0; y < game_map.height; ++y) {
            hlt::Location loc = {x, y};
            hlt::Site &site = game_map.getSite(loc);
            previous_ownership[{x, y}] = site.owner;
        }
    }
}

hlt::Move assign_move(hlt::GameMap &game_map, hlt::Location loc,
                      map<pair<unsigned short, unsigned short>, int> &destinations,
                      map<pair<unsigned short, unsigned short>,
                      vector<pair<hlt::Location, unsigned char>>> &origins) {
    hlt::Site &site = game_map.getSite(loc);
    double potential = INF;
    unsigned char best_d = STILL;
    hlt::Location target = {0, 0};

    bool staying_is_bad = site.strength + site.production + destinations[{loc.x, loc.y}] > MAX_STRENGTH;

    for (const int d : CARDINALS) {
        hlt::Location neighbor = game_map.getLocation(loc, d);

        if (destinations.find({neighbor.x, neighbor.y}) == destinations.end()) {
            destinations[{neighbor.x, neighbor.y}] = 0;
        }
        if (destinations[{neighbor.x, neighbor.y}] + site.strength > MAX_STRENGTH) {
            continue;
        }

        /* non aggresion pact */
        if (NAP && NAP_ON) {
            bool nap = false;
            for (const int neigh_d : CARDINALS) {
                hlt::Location next_neigh = game_map.getLocation(neighbor, neigh_d);
                if (game_map.getSite(next_neigh).owner != myID && game_map.getSite(next_neigh).owner != 0 &&
                    game_map.getSite(neighbor).owner == 0 && game_map.getSite(neighbor).strength != 0) {
                    nap = true;
                    pf_map[{neighbor.x, neighbor.y}] = INF;
                }
            }
            if (nap) {
                continue;   
            }
        }

        if (pf_map[{neighbor.x, neighbor.y}] <= potential) {
            potential = pf_map[{neighbor.x, neighbor.y}];
            best_d = d;
            target = neighbor;
        }

    }

    if (potential > INF / 10) {
        int min_strength = site.strength + destinations[{loc.x, loc.y}] + site.production;
        best_d = STILL;
        if (staying_is_bad) {
            for (const int d : CARDINALS) {
                hlt::Location neighbor = game_map.getLocation(loc, d);

                int strength = site.strength + destinations[{neighbor.x, neighbor.y}];
                if (strength < min_strength) {
                    min_strength = strength;
                    best_d = d;
                }
            }

        }
        return {loc, best_d};
    }

    if (!staying_is_bad) {
        for (const int d : CARDINALS) {
            hlt::Location neighbor = game_map.getLocation(loc, d);
            if (moves.find({neighbor, best_d}) != moves.end()) {
                return {loc, STILL};
            }
        }

        if (destinations[{loc.x, loc.y}] > 0) {
            return {loc, STILL};
        }
    }

    if (staying_is_bad) {
        int min_strength = MAX_STRENGTH + 1;
        for (const auto &origin : origins[{loc.x, loc.y}]) {
            auto neigh = game_map.getLocation(origin.first, origin.second);
            if (destinations[{neigh.x, neigh.y}] + site.strength < min_strength) {
                min_strength = destinations[{neigh.x, neigh.y}] + site.strength;
                best_d = origin.second;
            }
        }

        return {loc, best_d};
    }

    hlt::Site &target_site = game_map.getSite(target);
    if (target_site.owner != myID) {
        if (site.strength == MAX_STRENGTH || site.strength > target_site.strength) {
            return {loc, best_d};
        }
    } else if (site.strength >= site.production * delay) {
        return {loc, best_d};
    }

    return {loc, STILL};
}

double initial_potential(hlt::GameMap &game_map, hlt::Location loc) {
    const hlt::Site site = game_map.getSite(loc);
    if (site.owner == 0 && site.strength == 0) {
        int cnt = 0;
        for (const int d : CARDINALS) {
            hlt::Site &neigh = game_map.getSite(loc, d);
            if (neigh.owner != 0 && neigh.owner != myID) {
                cnt++;
            }
        }
        return ENEMY_ROI * cnt;
    } else if (site.production == 0) {
        return INF;
    } else {
        return static_cast<double>(site.strength) / site.production;
    }
}

void initial_check(hlt::GameMap &game_map) {
    set<unsigned char> players;
    for (unsigned short x = 0; x < game_map.width; ++x) {
        for (unsigned short y = 0; y < game_map.height; ++y) {
            players.insert(game_map.getSite({x, y}).owner);
        }
    }
    if (game_map.width == 20 || game_map.height == 20 || players.size() > 4) {
        DELTA_DEGRADATION = 0.42;
    }
    if (players.size() > 4) {
        NAP = true;
    } else {
        NAP = false;
    }
    if (game_map.height * game_map.width >= 1500 || game_map.width == 50 || game_map.height == 50) {
        DELTA_DEGRADATION = 0.42;
    }
}

int main() {
    hlt::GameMap game_map;
    getInit(myID, game_map);
    sendInit(BOT_NAME);

    // Initialize the previous ownership map
    update_previous_ownership(game_map);

    while(true) {
        moves.clear();
        turn++;
        getFrame(game_map);

        // if (turn == 0) {
        initial_check(game_map);
        // }

        auto comp = [](const std::tuple<double, double, int, hlt::Location>& a, const std::tuple<double, double, int, hlt::Location>& b) {
            return std::get<0>(a) > std::get<0>(b);
        };
        std::priority_queue<std::tuple<double, double, int, hlt::Location>,
                            std::vector<std::tuple<double, double, int, hlt::Location>>,
                            decltype(comp)> heap(comp);
        for (unsigned short x = 0; x < game_map.width; ++x) {
            for (unsigned short y = 0; y < game_map.height; ++y) {
                const hlt::Location loc = {x, y};
                const hlt::Site site = game_map.getSite(loc);
                if (site.owner == myID) {
                    continue;
                }
                const double potential = initial_potential(game_map, loc);
                heap.push({potential, potential, 0, loc});
            }
        }

        pf_map.clear();
        while (pf_map.size() < game_map.width * game_map.height) {
            auto top = heap.top();
            double potential = std::get<1>(top);
            int friendly_distance = std::get<2>(top);
            hlt::Location loc = std::get<3>(top);
            heap.pop();

            auto search = pf_map.find({loc.x, loc.y});
            if (search != pf_map.end()) {
                continue;
            }

            pf_map[{loc.x, loc.y}] = degrade_potential(potential, friendly_distance);
            for (const int d : CARDINALS) {
                hlt::Location neigh_loc = game_map.getLocation(loc, d);
                double neigh_potential = INF;

                const hlt::Site &neigh = game_map.getSite(loc, d);
                if (neigh.owner != myID) {
                    if (neigh.production && !neigh.owner) {
                        neigh_potential = (1 - ALPHA) * potential + ALPHA * (static_cast<double>(neigh.strength) / neigh.production);
                    }
                    heap.push({neigh_potential, neigh_potential, friendly_distance, neigh_loc});
                } else {
                    neigh_potential = degrade_potential(potential, friendly_distance + 1);
                    heap.push({neigh_potential, potential, friendly_distance + 1, neigh_loc});
                }
            }
        }
        map<pair<unsigned short, unsigned short>, int> destinations;
        map<pair<unsigned short, unsigned short>,
            vector<pair<hlt::Location, unsigned char>>> origins;

        vector<pair<pair<unsigned short, unsigned short>, int>> squares;
        for (unsigned short x = 0; x < game_map.width; ++x) {
            for (unsigned short y = 0; y < game_map.height; ++y) {
                const hlt::Location loc = {x, y};
                const hlt::Site site = game_map.getSite(loc);
                if (site.owner == myID && site.strength > 0) {
                    squares.push_back({{x, y}, site.strength});
                }
            }
        }

        sort(squares.begin(), squares.end(), [](
         pair<pair<unsigned short, unsigned short>, int> &a,
         pair<pair<unsigned short, unsigned short>, int> &b) {
            return a.second > b.second;
        });

        for (const auto &square : squares) {
            hlt::Move move = assign_move(game_map, {square.first.first, square.first.second}, destinations, origins);
            moves.insert(move);

            hlt::Location target = game_map.getLocation({square.first.first, square.first.second}, move.dir);

            destinations[{target.x, target.y}] += square.second;

            origins[{target.x, target.y}].push_back({{square.first.first, square.first.second}, opposite(move.dir)});
        }

        sendFrame(moves);

        
        if (has_been_attacked(game_map)) {
            delay = ATTACK_DELAY;
            // NAP = false;
        }

        update_previous_ownership(game_map);
    }

    return 0;
}
