//By Monica Moniot
#include "basic.h"
#define MAM_ALLOC_IMPLEMENTATION
#define MAM_ALLOC_SIZE_T int64
#define MAM_ALLOC_DEBUG
#include "mam_alloc.h"
#define PCG_IMPLEMENTATION
#include "pcg.h"

typedef MAM_ALLOC_SIZE_T relptr;

typedef struct String {
	char* ptr;
	uint64 size;
} String;

typedef struct Tokens {
	String* ptr;
	uint64 size;
} Tokens;


String to_str(char* c_str) {
	String str = {c_str, cast(uint, strlen(c_str))};
	return str;
}
// constexpr const String to_str(const char* c_str) {
// 	const String str = {c_str, cast(uint, strlen(c_str))};
// 	return str;
// }
bool to_int(const String str, uint32* ret_n) {
	uint32 n = 0;
	for_each_in(it, str.ptr, str.size) {
		char ch = *it;
		if('0' <= ch && ch <= '9') {
			n = 10*n + ch - '0';
		} else {
			return 0;
		}
	}
	if(ret_n) *ret_n = n;
	return 1;
}

int string_compare(const String str0, const String str1) {
	if(str0.size > str1.size) {
		return 1;
	} else if(str0.size < str1.size) {
		return -1;
	} else {
		return memcmp(str0.ptr, str1.ptr, str0.size);
	}
}

static const String str_slash = {"\"", 1};
static const String str_newline = to_str("\n");

struct Relstr {
	relptr ptr;
	uint64 size;
};


enum ItemType {
	ITEM_POTION,
	ITEM_WEAPON,
};
struct Item {
	int32 creator_pid;
	Relstr name;
	Relstr desc;
	int32 level;
	ItemType type;
};


constexpr int32 MAX_POWER = 100;
struct User {
	Relstr name;
	relptr cur_room;//the current room the player is in, can never be invalid
	int32 power;
	int32 items[5];
};

struct Monster {
	int32 type;
	int32 power;
	int32 max_power;
	int32 items[2];
};

constexpr uint MAX_ROOM_EXITS = 6;
struct Room {
	int32 creator_pid;
	Relstr name;
	Relstr desc;
	Relstr florid_desc;
	Relstr exit_names[MAX_ROOM_EXITS];
	relptr exit_rooms[MAX_ROOM_EXITS];
	Monster monster;
	uint exits_total;
};

struct MonsterType {
	int32 creator_pid;
	Relstr name;
	Relstr desc;
	int32 power_inv;
};


struct GameState {
	uint64 mem_size;
	PCG pcg;
	relptr users;//NOTE: user pids start from 1, not 0, so subtract 1 from the pid before indexing
	uint32 users_size;
	uint32 users_capacity;
	relptr head_room;
	uint32 rooms_total;
	relptr monster_types;
	uint32 monster_types_size;
	uint32 monster_types_capacity;
	relptr items;
	uint32 items_size;
	uint32 items_capacity;
	MamHeap heap;
};

struct Output {
	int32* users;
	String* responses;
	uint32 responses_size;
};

String to_str(GameState* game, Relstr relstr) {
	String str = {mam_get_ptr(char, &game->heap, relstr.ptr), relstr.size};
	return str;
}
String to_str(uint32 n, MamStack* stack) {
	String str = {mam_stack_push(char, stack, 0), 0};
	do {
		char ch = cast(char, n%10 + '0');
		mam_stack_extend(stack, 1);
		str.ptr[str.size] = ch;
		str.size += 1;
		n /= 10;
	} while(n > 0);
	for_each_lt(i, str.size/2) {
		swap(&str.ptr[i], &str.ptr[str.size - i - 1]);
	}
	return str;
}


static Relstr alloc_string(GameState* game, const String str) {
	Relstr string = {mam_heap_alloci(&game->heap, str.size), str.size};
	memcpy(mam_get_ptr(byte, &game->heap, string.ptr), str.ptr, str.size);
	return string;
}
static void free_string(GameState* game, const Relstr str) {
	mam_heap_freei(&game->heap, str.ptr);
}

static void push_string(MamStack* stack, String* base_str, const String str) {
	mam_stack_extend(stack, str.size);
	memcpy(&base_str->ptr[base_str->size], str.ptr, str.size);
	base_str->size += str.size;
}
static void push_num(MamStack* stack, String* base_str, uint32 n) {
	int start = base_str->size;
	do {
		char ch = cast(char, n%10 + '0');
		mam_stack_extend(stack, 1);
		base_str->ptr[base_str->size] = ch;
		base_str->size += 1;
		n /= 10;
	} while(n > 0);
	int total = base_str->size - start;
	for_each_lt(i, total/2) {
		swap(&base_str->ptr[start + i], &base_str->ptr[base_str->size - i - 1]);
	}
}
static String push_strings(MamStack* stack, const String* strs, uint size) {
	String ret = {mam_stack_push(char, stack, 0), 0};
	for_each_in(it, strs, size) {
		push_string(stack, &ret, *it);
	}
	return ret;
}

static int32 roll_dice(GameState* game, int32 low, int32 high, int32 count, String* response, MamStack* stack) {
	int32 total = 0;
	for_each_lt(i, count) {
		int32 roll = pcg_random_in(&game->pcg, low, high);
		push_num(stack, response, roll);
		if(i != count - 1) {
			push_string(stack, response, to_str(" + "));
		} else {
			push_string(stack, response, to_str(" = "));
		}
		total += roll;
	}
	push_num(stack, response, total);
	return total;
}



struct InternalOutput {
	int32* user_pids;
	String* responses;
	uint32 responses_size;
	uint32 responses_capacity;
};

static void output_response(InternalOutput* output, int32 user_pid, String response, MamStack* trans_stack) {
	if(output->responses_capacity == output->responses_size) {
		output->responses_capacity *= 2;
		auto new_user_pids = mam_stack_push(int32, trans_stack, output->responses_capacity);
		auto new_responses = mam_stack_push(String, trans_stack, output->responses_capacity);
		memcpy(new_user_pids, output->user_pids, output->responses_size);
		memcpy(new_responses, output->responses, output->responses_size);
		output->user_pids = new_user_pids;
		output->responses = new_responses;
	}
	output->user_pids[output->responses_size] = user_pid;
	output->responses[output->responses_size] = response;
	output->responses_size += 1;
}



static int32 alloc_monster_type(GameState* game, int32 creator_pid, const String name, const String desc, int32 power) {
	if(game->monster_types_capacity == game->monster_types_size) {
		game->monster_types_capacity *= 2;
		game->monster_types = mam_heap_realloci(&game->heap, game->monster_types, sizeof(MonsterType)*game->monster_types_capacity);
	}
	int32 pid = game->monster_types_size;
	auto monster_type = &mam_get_ptr(MonsterType, &game->heap, game->monster_types)[game->monster_types_size];
	game->monster_types_size += 1;
	monster_type->creator_pid = creator_pid;
	monster_type->name = alloc_string(game, name);
	monster_type->desc = alloc_string(game, desc);
	monster_type->power_inv = power;
	return pid + 1;
}
static MonsterType* get_monster_type(GameState* game, int32 monster_type_pid) {
	return &mam_get_ptr(MonsterType, &game->heap, game->monster_types)[monster_type_pid - 1];
}
static Monster* spawn_monster(GameState* game, int32 monster_type_pid, Room* room) {
	int32 power = 0;
	int32 power_inv = get_monster_type(game, monster_type_pid)->power_inv;
	for_each_lt(i, power_inv) {
		power += pcg_random_in(&game->pcg, 1, 6);
	}
	room->monster.type = monster_type_pid;
	room->monster.power = power;
	room->monster.max_power = power;
}
static void monster_died(GameState* game, Room* room) {
	memzero(&room->monster, sizeof(Monster));
}
//NOTE: maybe we should prevent giant strings?
relptr alloc_room(GameState* game, int32 creator_pid, const String name, const String desc, const String florid_desc, const String exit_name, relptr pre_room) {
	relptr room_id = mam_heap_alloci(&game->heap, sizeof(Room));
	Room* room = mam_get_ptr(Room, &game->heap, room_id);
	memzero(room, sizeof(Room));
	room->creator_pid = creator_pid;
	room->name = alloc_string(game, name);
	room->desc = alloc_string(game, desc);
	room->florid_desc = alloc_string(game, florid_desc);
	room->exit_names[0] = alloc_string(game, exit_name);
	room->exit_rooms[0] = pre_room;
	room->exits_total = 1;
	room->monster.type = 0;
	room->monster.power = 0;
	return room_id;
}
Room* get_room(GameState* game, relptr room_id) {
	return mam_get_ptr(Room, &game->heap, room_id);
}
static int get_direction_i(GameState* game, Room* room, String direction_str) {
	//returns -1 if it could not find a match
	int direction_i = -1;
	for_each_lt(i, room->exits_total) {
		if(string_compare(direction_str, to_str(game, room->exit_names[i])) == 0) {
			direction_i = i;
			break;
		}
	}
	return direction_i;
}

static User* get_user(GameState* game, int32 user_pid) {
	return &mam_get_ptr(User, &game->heap, game->users)[user_pid - 1];
}


static void user_died(GameState* game, int32 user_pid) {
	User* user = get_user(game, user_pid);
	user->cur_room = game->head_room;
	user->power = MAX_POWER;
}
static void response_user_moved(GameState* game, Room* room, String* response, MamStack* stack, InternalOutput* output) {
	push_string(stack, response, str_slash);
	push_string(stack, response, to_str(game, room->name));
	push_string(stack, response, to_str("\"\n"));
	push_string(stack, response, to_str(game, room->desc));
	push_string(stack, response, str_newline);
}
static void response_user_overexertion(GameState* game, User* user, String* response, MamStack* stack, InternalOutput* output) {
	push_string(stack, response, to_str("You put all of your strength into this, and promptly died of overexertion...\nYou wake up dazed back were you started:\n"));
	response_user_moved(game, get_room(game, game->head_room), response, stack, output);
}
static void response_user_slain(GameState* game, User* user, String* response, MamStack* stack, InternalOutput* output) {
	push_string(stack, response, to_str("You were slain...\nYou wake up dazed back were you started:\n"));
	response_user_moved(game, get_room(game, game->head_room), response, stack, output);
}
static void response_monster_overexertion(GameState* game, Monster* monster, String* response, MamStack* stack, InternalOutput* output) {
	auto monster_type = get_monster_type(game, monster->type);
	push_string(stack, response, str_slash);
	push_string(stack, response, to_str(game, monster_type->name));
	push_string(stack, response, to_str("\" put all of strength into this attack and died!\n"));
}
static void response_monster_slain(GameState* game, Monster* monster, String* response, MamStack* stack, InternalOutput* output) {
	auto monster_type = get_monster_type(game, monster->type);
	push_string(stack, response, to_str("\""));
	push_string(stack, response, to_str(game, monster_type->name));
	push_string(stack, response, to_str("\" was slain\n"));
}

int32 add_player(byte* game_memory, String username) {
	auto game = cast(GameState*, game_memory);
	if(game->users_size == game->users_capacity) {
		game->users_capacity *= 2;
		game->users = mam_heap_realloci(&game->heap, game->users, sizeof(User)*game->users_capacity);
	}
	auto users = mam_get_ptr(User, &game->heap, game->users);
	auto new_user = &users[game->users_size];
	game->users_size += 1;

	{
		new_user->name = alloc_string(game, username);
		new_user->cur_room = game->head_room;
		new_user->power = MAX_POWER;
	}
	return game->users_size;
}


void init_game(byte* game_memory) {
	auto game = cast(GameState*, game_memory);
	mam_heap_init(&game->heap, game->mem_size - sizeof(GameState));

	pcg_seed(&game->pcg, 7843);
	game->users_capacity = 8;
	game->users_size = 0;
	game->users = mam_heap_alloci(&game->heap, sizeof(User)*game->users_capacity);
	game->head_room = mam_heap_alloci(&game->heap, sizeof(Room));
	game->head_room = mam_heap_alloci(&game->heap, sizeof(Room));
	auto head_room = mam_get_ptr(Room, &game->heap, game->head_room);
	{
		head_room->creator_pid = add_player(game_memory, to_str("Monica"));
		head_room->name = alloc_string(game, to_str("Cave Entrance"));
		head_room->desc = alloc_string(game, to_str("The possibilities are endless."));
		head_room->florid_desc = alloc_string(game, to_str("The cave is filled with brilliant crystals, their soft glow lighting the caverns below. The surrounding walls are loose and malleable. There are 8 exabytes worth of possibilities available to you."));
		head_room->exits_total = 0;
	}
	game->rooms_total = 1;

	game->monster_types_capacity = 8;
	game->monster_types_size = 0;
	game->monster_types = mam_heap_alloci(&game->heap, sizeof(MonsterType)*game->monster_types_capacity);
}

int32 connect_player(byte* game_memory, String username) {//NOTE: this has no authentication!!
	auto game = cast(GameState*, game_memory);
	auto users = mam_get_ptr(User*, &game->heap, game->users);
	User* this_user = 0;
	int32 user_pid = 0;
	for_each_lt(i, game->users_size) {
		if(string_compare(to_str(game, users[i]->name), username) == 0) {
			this_user = users[i];
			user_pid = i + 1;
		}
	}
	return user_pid;
}

// int32 disconnect_player(byte* game_memory, int32 user_pid) {
//
// }


Tokens tokenize(String text, MamStack* stack) {
	//", \n, 0, \\n, \\t,
	char* raw_text = mam_stack_push(char, stack, text.size);
	Tokens tokens = {mam_stack_push(String, stack, 0), 0};

	bool is_word = 0;
	bool is_in_quotes = 0;
	bool just_slash = 0;
	int ascii_slash = 0;
	char ascii_num = 0;

	String* cur_word = tokens.ptr - 1;
	char* cur_ch = raw_text;

	for_each_lt(i, text.size + 1) {
		String error_str = {0, 0};
		char write_ch = 0;//we ignore all null characters
		bool is_word_start = 0;
		if(i < text.size) {
			char ch = text.ptr[i];
			if(is_in_quotes) {
				if(ascii_slash) {
					if('0' <= ch && ch <= '9') {
						ascii_num += ch - '0';
					} else if('a' <= ch && ch <= 'f') {
						ascii_num += ch - 'a' + 10;
					} else if('A' <= ch && ch <= 'F') {
						ascii_num += ch - 'A' + 10;
					} else {
						error_str = to_str("Please use two hexidecimal digits when using \\x for ascii characters.\n");
					}
					if(ascii_slash == 1) {
						ascii_slash = 2;
						ascii_num *= 16;
					} else {
						if(ascii_num == '\"') {
							error_str = to_str("Sorry, escaping a \" is not allowed.\n");
						} else if(ascii_num == '\x08') {
							error_str = to_str("Sorry, \\x08 is not allowed.\n");
						} else if(ascii_num == '\x0d') {
							error_str = to_str("Sorry, \\x0d is not allowed.\n");
						} else if(ascii_num == 0) {
							error_str = to_str("Sorry, escaping a 0 is not allowed.\n");
						} else {
							write_ch = ascii_num;
						}
						ascii_slash = 0;
						ascii_num = 0;
					}
				} else if(just_slash) {
					just_slash = 0;
					if(ch == 'n') {
						write_ch = '\n';
					} else if(ch == 't') {
						write_ch = '\t';
					} else if(ch == 'x') {
						ascii_slash = 1;
					} else if(ch == '\"') {
						error_str = to_str("Sorry, escaping a \" is not allowed.\n");
					} else if(ch == '0') {
						error_str = to_str("Sorry, escaping a 0 is not allowed.\n");
					} else {
						write_ch = ch;
					}
				} else if(ch == '\"') {
					is_in_quotes = 0;
					is_word = 0;
					if(cur_word->size == 0) {
						error_str = to_str("Sorry, 0 length strings are not allowed.\n");
					}
				} else if(ch == '\\') {
					just_slash = 1;
				} else {
					write_ch = ch;
				}
			} else if(is_word) {
				if(ch == ' ' || ch == '\t' || ch == '\n') {
					is_word = 0;
				} else if(ch == '\"') {
					is_in_quotes = 1;
					is_word_start = 1;
				} else {
					write_ch = ch;
				}
			} else {
				if(ch == '\"') {
					is_word = 1;
					is_word_start = 1;
					is_in_quotes = 1;
				} else if(!(ch == ' ' || ch == '\t' || ch == '\n')) {
					is_word = 1;
					is_word_start = 1;
					write_ch = ch;
				}
			}
		} else {
			if(is_in_quotes) {
				error_str = to_str("Parse error, an unclosed \" was encountered.\n");
			}
		}
		if(error_str.ptr) {
			//remove everything added to memory and return the error message
			mam_stack_pop(stack);
			mam_stack_pop(stack);
			tokens = {mam_stack_push(String, stack, 1), 1};
			tokens.ptr[0] = error_str;
			tokens.size = 0;
			return tokens;
		} else {
			if(is_word_start) {
				//create more space for the new token
				mam_checki(stack, ptr_dist(stack, tokens.ptr), sizeof(String)*tokens.size);
				cur_word += 1;
				tokens.size += 1;
				mam_stack_extend(stack, sizeof(String));
				cur_word->ptr = cur_ch;
				cur_word->size = 0;
			}
			if(write_ch) {
				*cur_ch = write_ch;
				cur_ch += 1;
				cur_word->size += 1;
			}
		}
	}
	return tokens;
}


inline int32 get_max_power_inv(int32 power, int32 mult) {
	return (power + mult - 1)/mult;
}
inline bool has_enough_power(int32 power, int32 mult, int32 inv) {
	return inv <= get_max_power_inv(power, mult);
}

constexpr int32 MONSTER_POWER_MULT = 4;


#define DECL_COMMAND(name) static void MACRO_CAT(command_, name)(GameState* game, int32 user_pid, Tokens tokens, InternalOutput* output, MamStack* trans_stack)
#include <stdio.h>

constexpr int32 ATTACK_POWER_MULT = 2;
static int32 monster_decide_power_inv(GameState* game, Monster* monster) {
	int32 cur_power = monster->power;
	int32 max_power = monster->max_power;
	int32 max_power_inv = get_max_power_inv(cur_power, ATTACK_POWER_MULT);
	float desperation = (.8 - .6*cast(float, cur_power)/(max_power));
	//when the monster is at max health, it will spend ~.1 of it's health on attacking
	//near 0, it will spend ~.9 of it's health
	printf("(desperation: %f, cur_power: %d, max_power: %d)", desperation, cur_power, max_power);
	int32 desired_power_inv = cast(int32, max_power_inv*desperation + .5);
	desired_power_inv = (desired_power_inv > 1) ? desired_power_inv : 1;
	desired_power_inv = (desired_power_inv < max_power_inv) ? desired_power_inv : max_power_inv;
	return desired_power_inv;
}
DECL_COMMAND(attack) {
	User* cur_user = get_user(game, user_pid);
	Room* user_room = get_room(game, cur_user->cur_room);
	uint32 power_inv;
	if(tokens.size == 2 && to_int(tokens.ptr[1], &power_inv) && power_inv > 0) {
		if(user_room->monster.type) {
			if(has_enough_power(cur_user->power, ATTACK_POWER_MULT, power_inv)) {
				auto monster = &user_room->monster;
				auto monster_type = get_monster_type(game, monster->type);
				String response = {mam_stack_push(char, trans_stack, 0), 0};
				push_string(trans_stack, &response, to_str("You spent "));
				push_num(trans_stack, &response, ATTACK_POWER_MULT*power_inv);
				push_string(trans_stack, &response, to_str(" power and attacked \""));
				push_string(trans_stack, &response, to_str(game, monster_type->name));
				push_string(trans_stack, &response, to_str("\" for:\n"));
				{//attack monster
					int32 damage = roll_dice(game, 1, 6, power_inv, &response, trans_stack);
					push_string(trans_stack, &response, to_str(" damage.\n"));
					monster->power -= damage;
				}
				bool did_monster_die = monster->power <= 0;
				if(did_monster_die) {
					response_monster_slain(game, monster, &response, trans_stack, output);
					monster_died(game, user_room);
				}
				cur_user->power -= ATTACK_POWER_MULT*power_inv;
				if(cur_user->power <= 0) {
					response_user_overexertion(game, cur_user, &response, trans_stack, output);
					user_died(game, user_pid);
				} else if(!did_monster_die) {
					push_string(trans_stack, &response, to_str("\""));
					push_string(trans_stack, &response, to_str(game, monster_type->name));
					push_string(trans_stack, &response, to_str("\" counter-attacks for:\n"));
					{
						int32 monster_power_inv = monster_decide_power_inv(game, monster);
						monster->power -= ATTACK_POWER_MULT*monster_power_inv;
						int32 damage = roll_dice(game, 1, 6, monster_power_inv, &response, trans_stack);
						push_string(trans_stack, &response, to_str(" damage.\n"));
						cur_user->power -= damage;
					}
					//TODO: handle death
					if(monster->power <= 0) {
						response_monster_overexertion(game, monster, &response, trans_stack, output);
						monster_died(game, user_room);
					}
					if(cur_user->power <= 0) {
						response_user_slain(game, cur_user, &response, trans_stack, output);
						user_died(game, user_pid);
					}
				}
				output_response(output, user_pid, response, trans_stack);
			} else {
				String strs[6] = {
					to_str("Your power is too low to make that attack; you have "), to_str(cur_user->power, trans_stack), to_str(" power, but tried to spend "), to_str(ATTACK_POWER_MULT*power_inv, trans_stack), to_str(".\n"),
				};
				output_response(output, user_pid, push_strings(trans_stack, strs, 6), trans_stack);
			}
		} else {
			output_response(output, user_pid, to_str("There is no monster present to attack\n"), trans_stack);
		}
	} else {
		output_response(output, user_pid, to_str("Usage: attack \"power investment\"\n"), trans_stack);
	}
}

DECL_COMMAND(spawn) {
	User* cur_user = get_user(game, user_pid);
	Room* user_room = get_room(game, cur_user->cur_room);
	uint32 power_inv;
	if(tokens.size == 4 && to_int(tokens.ptr[1], &power_inv) && power_inv > 0) {
		if(!user_room->monster.type) {
			if(has_enough_power(cur_user->power, MONSTER_POWER_MULT, power_inv)) {
				cur_user->power -= MONSTER_POWER_MULT*power_inv;
				int32 monster_pid = alloc_monster_type(game, user_pid, tokens.ptr[2], tokens.ptr[3], power_inv);
				String response = {mam_stack_push(char, trans_stack, 0), 0};
				mam_check(response.ptr, response.size);
				push_string(trans_stack, &response, to_str("You spent "));
				mam_check(response.ptr, response.size);
				push_num(trans_stack, &response, MONSTER_POWER_MULT*power_inv);
				mam_check(response.ptr, response.size);
				push_string(trans_stack, &response, to_str(" power.\n"));
				mam_check(response.ptr, response.size);
				push_string(trans_stack, &response, to_str("You successfully spawned the monster, \""));
				mam_check(response.ptr, response.size);
				push_string(trans_stack, &response, tokens.ptr[2]);
				push_string(trans_stack, &response, to_str("\". It has a power level of:\n"));
				{//spawn_monster(game, monster_pid, user_room);
					int32 power = roll_dice(game, 1, 6, power_inv, &response, trans_stack);
					push_string(trans_stack, &response, str_newline);
					user_room->monster.type = monster_pid;
					user_room->monster.power = power;
					user_room->monster.max_power = power;
				}
				if(cur_user->power <= 0) {
					user_died(game, user_pid);
					response_user_overexertion(game, cur_user, &response, trans_stack, output);
				}
				output_response(output, user_pid, response, trans_stack);
			} else {
				String strs[6] = {
					to_str("Your power is too low to spawn a monster that strong; you have "), to_str(cur_user->power, trans_stack), to_str(" power, but tried to spend "), to_str(MONSTER_POWER_MULT*power_inv, trans_stack), to_str(".\n"),
				};
				output_response(output, user_pid, push_strings(trans_stack, strs, 6), trans_stack);
			}
		} else {
			String strs[3] = {
				to_str("The monster \""), to_str(game, get_monster_type(game, user_room->monster.type)->name), to_str("\" is present, preventing you from spawning another monster.\n"),
			};
			output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
		}
	} else {
		output_response(output, user_pid, to_str("Usage: spawn \"power investment\" \"monster name\" \"description\"\n"), trans_stack);
	}
}

DECL_COMMAND(move) {
	User* cur_user = get_user(game, user_pid);
	Room* user_room = get_room(game, cur_user->cur_room);
	if(tokens.size == 2) {
		int direction_i = get_direction_i(game, user_room, tokens.ptr[1]);
		if(direction_i != -1) {//move player between rooms
			relptr room_i = user_room->exit_rooms[direction_i];
			Room* room = mam_get_ptr(Room, &game->heap, room_i);
			cur_user->cur_room = room_i;
			//construct the response string
			String response = {mam_stack_push(char, trans_stack, 0), 0};
			push_string(trans_stack, &response, to_str("\""));
			push_string(trans_stack, &response, to_str(game, room->name));
			push_string(trans_stack, &response, to_str("\"\n"));
			push_string(trans_stack, &response, to_str(game, room->desc));
			output_response(output, user_pid, response, trans_stack);
		} else {
			String strs[3] = {
				to_str("You couldn't find an exit in the \""), tokens.ptr[1], to_str("\" direction.\n"),
			};
			output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
		}
	} else {
		output_response(output, user_pid, to_str("Usage: move \"direction\"\n"), trans_stack);
	}
}

DECL_COMMAND(status) {
	User* cur_user = get_user(game, user_pid);
	Room* user_room = get_room(game, cur_user->cur_room);
	String strs[3] = {
		to_str("You currently have "), to_str(cur_user->power, trans_stack), to_str(" power\n"),
	};
	output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
}

DECL_COMMAND(list) {
	User* cur_user = get_user(game, user_pid);
	Room* user_room = get_room(game, cur_user->cur_room);
	//construct the response string
	String response = {mam_stack_push(char, trans_stack, 0), 0};
	//find any players that are in the same room
	bool users_are_present = 0;
	for_each_lt(i, game->users_size) {
		auto pid = i + 1;
		if(pid == user_pid) continue;
		auto user = get_user(game, pid);
		if(user->cur_room == cur_user->cur_room) {
			if(!users_are_present) {
				users_are_present = 1;
				push_string(trans_stack, &response, to_str("You see several of your friends around you:\n\""));
			} else {
				push_string(trans_stack, &response, to_str(",\n\""));
			}
			push_string(trans_stack, &response, to_str(game, user->name));
			push_string(trans_stack, &response, to_str("\""));
		}
	}
	if(users_are_present) {
		push_string(trans_stack, &response, to_str(".\n"));
	}
	if(user_room->monster.type) {
		push_string(trans_stack, &response, to_str("The monster, \""));
		push_string(trans_stack, &response, to_str(game, get_monster_type(game, user_room->monster.type)->name));
		push_string(trans_stack, &response, to_str("\" is present.\n"));
	} else if(!users_are_present) {
		push_string(trans_stack, &response, to_str("The room is empty.\n"));
	}
	output_response(output, user_pid, response, trans_stack);
}

DECL_COMMAND(ls) {
	User* cur_user = get_user(game, user_pid);
	Room* user_room = get_room(game, cur_user->cur_room);
	if(user_room->exits_total > 0) {
		String response = {mam_stack_push(char, trans_stack, 0), 0};
		push_string(trans_stack, &response, to_str("You glance around; you see the following directions you can move in:"));
		for_each_lt(i, user_room->exits_total) {
			push_string(trans_stack, &response, to_str("\n\""));
			push_string(trans_stack, &response, to_str(game, user_room->exit_names[i]));
			push_string(trans_stack, &response, to_str("\" -> "));
			Room* exit_room = mam_get_ptr(Room, &game->heap, user_room->exit_rooms[i]);
			push_string(trans_stack, &response, to_str(game, exit_room->name));
		}
		push_string(trans_stack, &response, str_newline);
		output_response(output, user_pid, response, trans_stack);
	} else {
		output_response(output, user_pid, to_str("You glance around; but there is nowhere to go!\n"), trans_stack);
	}
}

#define STATICSTR(name) static const String MACRO_CAT(str_, name) = {#name, cast(uint, strlen(#name))}
STATICSTR(help);
STATICSTR(echo);
STATICSTR(ls);
STATICSTR(look);
STATICSTR(list);
STATICSTR(status);
STATICSTR(move);
STATICSTR(dig);
STATICSTR(redig);
STATICSTR(repath);
STATICSTR(spawn);
STATICSTR(attack);
// STATICSTR(take);


DECL_COMMAND(help) {
	if(tokens.size == 1) {
		//construct the response string
		String strs[24] = {
			to_str("These are the available commands:\n"),
			str_help, str_newline,
			str_echo, str_newline,
			str_ls, str_newline,
			str_look, str_newline,
			str_list, str_newline,
			str_move, str_newline,
			str_dig, str_newline,
			str_redig, str_newline,
			str_repath, str_newline,
			str_spawn, str_newline,
			str_attack, str_newline,
			to_str("Type help \"command name\" to get more information on a particular command.\n")
		};
		output_response(output, user_pid, push_strings(trans_stack, strs, 24), trans_stack);
	} else {
		String help_command = tokens.ptr[1];
		if(string_compare(help_command, str_ls) == 0) {
			output_response(output, user_pid, to_str("Usage: ls\n"), trans_stack);
		} else if(string_compare(help_command, str_echo) == 0) {
			output_response(output, user_pid, to_str("Usage: echo ...\n"), trans_stack);
		} else if(string_compare(help_command, str_look) == 0) {
			output_response(output, user_pid, to_str("Usage: look\n"), trans_stack);
		} else if(string_compare(help_command, str_list) == 0) {
			output_response(output, user_pid, to_str("Usage: list\n"), trans_stack);
		} else if(string_compare(help_command, str_move) == 0) {
			output_response(output, user_pid, to_str("Usage: move \"direction\"\n"), trans_stack);
		} else if(string_compare(help_command, str_dig) == 0) {
			output_response(output, user_pid, to_str("Usage: dig \"room name\" \"exit direction\" \"entrance direction\" \"description\" \"florid description\"\n"), trans_stack);
		} else if(string_compare(help_command, str_redig) == 0) {
			output_response(output, user_pid, to_str("Usage: redig \"room name\" \"description\" \"florid description\"\n"), trans_stack);
		} else if(string_compare(help_command, str_repath) == 0) {
			output_response(output, user_pid, to_str("Usage: repath \"previous exit direction\" \"new exit direction\"\n"), trans_stack);
		} else if(string_compare(help_command, str_spawn) == 0) {
			output_response(output, user_pid, to_str("Usage: spawn \"power investment\" \"monster name\" \"description\"\n"), trans_stack);
		} else if(string_compare(help_command, str_attack) == 0) {
			output_response(output, user_pid, to_str("Usage: attack \"power investment\"\n"), trans_stack);
		} else {
			String response = {mam_stack_push(char, trans_stack, 0), 0};
			push_string(trans_stack, &response, to_str("\""));
			push_string(trans_stack, &response, help_command);
			push_string(trans_stack, &response, to_str("\" is not a recognized command.\n"));
			output_response(output, user_pid, response, trans_stack);
		}
	}
}


Output* command_game(byte* game_memory, byte* trans_memory, int32 user_pid, String command) {
	auto game = cast(GameState*, game_memory);
	auto trans_stack = ptr_add(MamStack, trans_memory, sizeof(uint64));
	mam_stack_init(trans_stack, *cast(uint64*, trans_memory) - sizeof(uint64));

	InternalOutput* output = mam_stack_push(InternalOutput, trans_stack, 1);
	uint32 responses_capacity = 4;
	output->user_pids = mam_stack_push(int32, trans_stack, responses_capacity);
	output->responses = mam_stack_push(String, trans_stack, responses_capacity);
	output->responses_size = 0;

	Tokens tokens = tokenize(command, trans_stack);

	if(tokens.size > 0) {
		User* cur_user = get_user(game, user_pid);
		String head = tokens.ptr[0];
		Room* user_room = mam_get_ptr(Room, &game->heap, cur_user->cur_room);
		if(string_compare(head, str_help) == 0) {
			command_help(game, user_pid, tokens, output, trans_stack);
		} else if(string_compare(head, str_echo) == 0) {
			if(tokens.size > 1) {
				String response = {mam_stack_push(char, trans_stack, 0), 0};
				push_string(trans_stack, &response, to_str("You yell into the void, and the void answers back:\n"));
				for_each_in_range(i, 1, tokens.size - 1) {
					push_string(trans_stack, &response, to_str("\""));
					push_string(trans_stack, &response, tokens.ptr[i]);
					push_string(trans_stack, &response, to_str("\" "));
				}
				push_string(trans_stack, &response, str_newline);
				output_response(output, user_pid, response, trans_stack);
			} else {
				output_response(output, user_pid, to_str("You yell into the void and hear nothing answer back.\n"), trans_stack);
			}
		} else if(string_compare(head, str_ls) == 0) {
			command_ls(game, user_pid, tokens, output, trans_stack);
		} else if(string_compare(head, str_look) == 0) {
			//construct the response string
			User* creator = get_user(game, user_room->creator_pid);
			String strs[7] = {
				to_str("\""), to_str(game, user_room->name), to_str("\"\n"),
				to_str(game, user_room->florid_desc),
				to_str("\nIt's clear that "), to_str(game, creator->name), to_str(" built this room.\n"),
			};
			output_response(output, user_pid, push_strings(trans_stack, strs, 7), trans_stack);
		} else if(string_compare(head, str_list) == 0) {
			command_list(game, user_pid, tokens, output, trans_stack);
		} else if(string_compare(head, str_status) == 0) {
			command_status(game, user_pid, tokens, output, trans_stack);
		} else if(string_compare(head, str_move) == 0) {
			command_move(game, user_pid, tokens, output, trans_stack);
		} else if(string_compare(head, str_dig) == 0) {
			if(user_room->exits_total < MAX_ROOM_EXITS) {
				if(tokens.size == 6) {
					//add the room
					uint exit_i = user_room->exits_total;
					relptr room_id = alloc_room(game, user_pid, tokens.ptr[1], tokens.ptr[4], tokens.ptr[5], tokens.ptr[3], cur_user->cur_room);
					user_room->exit_names[exit_i] = alloc_string(game, tokens.ptr[2]);
					user_room->exit_rooms[exit_i] = room_id;
					user_room->exits_total += 1;
					//move the user between rooms
					cur_user->cur_room = room_id;
					String strs[3] = {
						to_str("You successfully dug out the room, \""), tokens.ptr[1], to_str("\".\n"),
					};
					output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
				} else {
					output_response(output, user_pid, to_str("Usage: dig \"room name\" \"exit direction\" \"entrance direction\" \"description\" \"florid description\"\n"), trans_stack);
				}
			} else {
				output_response(output, user_pid, to_str("You attempt to dig but there are no surfaces left to dig out! There are too many exits leaving this room already.\n"), trans_stack);
			}
		} else if(string_compare(head, str_repath) == 0) {
			if(tokens.size == 3) {
				int direction_i = get_direction_i(game, user_room, tokens.ptr[1]);
				if(direction_i != -1) {
					free_string(game, user_room->exit_names[direction_i]);
					user_room->exit_names[direction_i] = alloc_string(game, tokens.ptr[2]);
					String strs[3] = {
						to_str("You successfully repathed the exit in the, \""), tokens.ptr[2], to_str("\" direction.\n"),
					};
					output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
				} else {
					String strs[3] = {
						to_str("You couldn't find an exit in the \""), tokens.ptr[1], to_str("\" direction.\n")
					};
					output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
				}
			} else {
				output_response(output, user_pid, to_str("Usage: repath \"previous exit direction\" \"new exit direction\"\n"), trans_stack);
			}
		} else if(string_compare(head, str_redig) == 0) {
			if(user_room->creator_pid == user_pid) {
				if(tokens.size == 4) {
					free_string(game, user_room->name);
					free_string(game, user_room->desc);
					free_string(game, user_room->florid_desc);
					user_room->name = alloc_string(game, tokens.ptr[1]);
					user_room->desc = alloc_string(game, tokens.ptr[2]);
					user_room->florid_desc = alloc_string(game, tokens.ptr[3]);
					String strs[3] = {
						to_str("You successfully redug the room, \""), tokens.ptr[1], to_str("\".\n"),
					};
					output_response(output, user_pid, push_strings(trans_stack, strs, 3), trans_stack);
				} else {
					output_response(output, user_pid, to_str("Usage: redig \"room name\" \"description\" \"florid description\"\n"), trans_stack);
				}
			} else {
				output_response(output, user_pid, to_str("Only the creator of this room can redig it\n"), trans_stack);
			}
		} else if(string_compare(head, str_spawn) == 0) {
			command_spawn(game, user_pid, tokens, output, trans_stack);
		} else if(string_compare(head, str_attack) == 0) {
			command_attack(game, user_pid, tokens, output, trans_stack);
		} else {
			String response = {mam_stack_push(char, trans_stack, 0), 0};
			push_string(trans_stack, &response, to_str("No command was found with the name \""));
			push_string(trans_stack, &response, head);
			push_string(trans_stack, &response, to_str("\".\n"));
			output_response(output, user_pid, response, trans_stack);
		}
	} else if(tokens.ptr[0].ptr) {
		output_response(output, user_pid, tokens.ptr[0], trans_stack);
	} else {
		output_response(output, user_pid, to_str("Please enter a command; type \"help\" for a list of commands.\n"), trans_stack);
	}
	return cast(Output*, output);
}


#include <stdio.h>

// #define KEY_UP 72
// #define KEY_DOWN 80
// #define KEY_LEFT 75
// #define KEY_RIGHT 77

int main() {
	byte* game_memory = cast(byte*, malloc(16*MEGABYTE));
	*cast(uint64*, game_memory) = 16*MEGABYTE;
	init_game(game_memory);

	byte* trans_memory = cast(byte*, malloc(16*MEGABYTE));
	*cast(uint64*, trans_memory) = 16*MEGABYTE;

	int32 user_pid = 1;
	char* command_buffer = mmam_tape_newb(char);
	printf("Welcome, type help for a list of commands.\n>");
	while(1) {
		char c = getchar();
		if(c == '\n') {
			String command = {command_buffer, cast(uint32, mam_tape_sizeb(command_buffer))};
			Output* output = command_game(game_memory, trans_memory, user_pid, command);
			for_each_lt(i, output->responses_size) {
				String str = output->responses[i];
				printf("%.*s", cast(int, str.size), str.ptr);
			}
			printf("user>");
			//reset the command tape
			mam_tape_resetb(command_buffer);

			byte* new_game_memory = cast(byte*, malloc(16*MEGABYTE));
			memcpy(new_game_memory, game_memory, 16*MEGABYTE);
			free(game_memory);
			game_memory = new_game_memory;

			memzero(trans_memory, 16*MEGABYTE);
			*cast(uint64*, trans_memory) = 16*MEGABYTE;
		} else if(c == '`') {
			break;
		} else {
			mmam_tape_appendb(&command_buffer, c);
		}
	}

	return 0;
}
