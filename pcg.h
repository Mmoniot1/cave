/////////////////////////////////////////////////////////////
// NOTICE: This file was heavily modified by Monica Moniot //
// Thank you to the original author, Melissa O'Neill       //
/////////////////////////////////////////////////////////////
/*
 * PCG Random Number Generation for C.
 *
 * Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For additional information about the PCG random number generation scheme,
 * including its license and other licensing options, visit
 *
 *     http://www.pcg-random.org
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef INCLUDE__PCG_H
#define INCLUDE__PCG_H


#define PCG__DECLS static inline
#ifdef PCG_STATIC
 #define PCG__DECLR static
#else
 #define PCG__DECLR
#endif

#ifdef PCG_NO_STDINT
 #ifndef PCG_INT32
  #define PCG_INT32 int
 #endif
 #ifndef PCG_INT64
  #define PCG_INT64 long long
 #endif
#else
 #include <stdint.h>
 #ifndef PCG_INT32
  #define PCG_INT32 int32_t
 #endif
 #ifndef PCG_INT64
  #define PCG_INT64 int64_t
 #endif
#endif
////////////////////////////////////////////////////////////////////////////
// ~~~LIBRARY CONTENTS~~~
// PCG:
//     A struct that contains the entire state of a pcg rng
//
// pcg_seeds(rng, initstate, initseq):
//     Seed the rng, either seeds or seed must be called to initialize a rng
//     Specified in two parts, state initializer and a
//     sequence selection constant (a.k.a. stream id)
// pcg_seed(rng, seed):
//     Sets both the state initializer and stream id
//     to the given seed
//
// pcg_random(rng):
//     Generate a uniformly distributed 32-bit random integer
//
// pcg_advance(rng, delta):
//     Advances the rng stream by delta steps in lg time
//     So pcg_advance(rng, 3) behaves as though pcg_random(rng) was called 3 times
//     Also takes negative numbers to reverse the rng stream
//
// pcg_random_in(rng, lower, upper):
//     Generate a uniformly distributed int32, r, where lower <= r <= upper
// pcg_random_uniform(rng):
//     Generate a uniformly distributed float, r, where 0 <= r < 1
// pcg_random_uniform_in(rng):
//     Generate a uniformly distributed float, r, where 0 <= r <= 1
// pcg_random_uniform_ex(rng):
//     Generate a uniformly distributed float, r, where 0 < r < 1
//
// PCGF:
//     A PCG_INT32 that contains the entire state of a fast pcg rng.
//     Works identically to a normal pcg rng, except it is smaller and
//     faster, but produces much lower quality numbers.
//     To get the PCGF varient of any particular function, postfix it with "f"
//     (there do not exist a pcg_seedsf or pcg_advancef function)
//

typedef struct PCG {// Only modify the internals if you are a stats PhD.
    PCG_INT64 state;             // RNG state.  All values are possible.
    PCG_INT64 inc;               // Controls which RNG sequence (stream) is
                                // selected. Must *always* be odd.
} PCG;
typedef PCG_INT32 PCGF;

// If you *must* statically initialize it, use this.
#define PCG_INITIALIZER {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL}


PCG__DECLR void pcg_seeds(PCG* rng, PCG_INT64 initstate, PCG_INT64 sequence);
PCG__DECLS void pcg_seed(PCG* rng, PCG_INT64 seed);

PCG__DECLR PCG_INT32 pcg_random(PCG* rng);

PCG__DECLR void pcg_advance(PCG* rng, PCG_INT64 delta);

PCG__DECLR PCG_INT32 pcg_random_in(PCG* rng, PCG_INT32 lower, PCG_INT32 upper);
PCG__DECLS float pcg_random_uniform(PCG* rng);
PCG__DECLS float pcg_random_uniform_in(PCG* rng);
PCG__DECLS float pcg_random_uniform_ex(PCG* rng);


PCG__DECLS void pcg_seedf(PCGF* rng, PCG_INT32 seed);

PCG__DECLR PCG_INT32 pcg_randomf(PCGF* state);

PCG__DECLS PCG_INT32 pcg_random_inf(PCGF* rng, PCG_INT32 lower, PCG_INT32 upper);
PCG__DECLS float pcg_random_uniformf(PCGF* rng);
PCG__DECLS float pcg_random_uniform_inf(PCGF* rng);
PCG__DECLS float pcg_random_uniform_exf(PCGF* rng);

PCG__DECLS PCG_INT64 pcg__advance_lcg_64(PCG_INT64 state, PCG_INT64 delta, PCG_INT64 cur_mult, PCG_INT64 cur_plus);




PCG__DECLS PCG_INT64 pcg__advance_lcg_64(PCG_INT64 state, PCG_INT64 delta, PCG_INT64 cur_mult, PCG_INT64 cur_plus) {
    PCG_INT64 acc_mult = 1u;
    PCG_INT64 acc_plus = 0u;
    while (delta > 0) {
        if (delta & 1) {
            acc_mult *= cur_mult;
            acc_plus = acc_plus * cur_mult + cur_plus;
        }
        cur_plus = (cur_mult + 1) * cur_plus;
        cur_mult *= cur_mult;
        delta /= 2;
    }
    return acc_mult * state + acc_plus;
}

PCG__DECLS void pcg_seed(PCG* rng, PCG_INT64 seed) {
    pcg_seeds(rng, seed, seed);
}

PCG__DECLS float pcg_random_uniform(PCG* rng) {
	PCG_INT32 r = pcg_random(rng);
	return r*(1/(((float)~0u) + 1));
}
PCG__DECLS float pcg_random_uniform_in(PCG* rng) {
	PCG_INT32 r = pcg_random(rng);
	return ((float)r)/((float)~0u);
}
PCG__DECLS float pcg_random_uniform_ex(PCG* rng) {
	PCG_INT32 r = pcg_random(rng);
	return (((float)r) + 1)/(((float)~0u) + 2);
}

PCG__DECLS void pcg_seedf(PCGF* rng, PCG_INT32 seed) {
	*rng = seed;
}

PCG__DECLS PCG_INT32 pcg_random_inf(PCGF* rng, PCG_INT32 lower, PCG_INT32 upper) {
	return (pcg_randomf(rng)%(upper - lower + 1)) + lower;
}
PCG__DECLS float pcg_random_uniformf(PCGF* rng) {
	return ((float)pcg_randomf(rng))*(1/(((float)~0u) + 1));
}
PCG__DECLS float pcg_random_uniform_inf(PCGF* rng) {
	return ((float)pcg_randomf(rng))/((float)~0u);
}
PCG__DECLS float pcg_random_uniform_exf(PCGF* rng) {
	return (((float)pcg_randomf(rng)) + 1)/(((float)~0u) + 2);
}

#endif

#ifdef PCG_IMPLEMENTATION
#undef PCG_IMPLEMENTATION

PCG__DECLR PCG_INT32 pcg_random(PCG* rng) {
    PCG_INT64 oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + rng->inc;
    PCG_INT32 xorshifted = (PCG_INT32)(((oldstate >> 18u) ^ oldstate) >> 27u);
    PCG_INT32 rot = (PCG_INT32)(oldstate >> 59u);
    return (PCG_INT32)((xorshifted >> rot) | (xorshifted << (((PCG_INT32)(-(PCG_INT32)rot)) & 31)));
}

PCG__DECLR void pcg_seeds(PCG* rng, PCG_INT64 initstate, PCG_INT64 sequence) {
    rng->state = 0U;
    rng->inc = (sequence << 1u) | 1u;
    pcg_random(rng);
    rng->state += initstate;
    pcg_random(rng);
}

PCG__DECLR void pcg_advance(PCG* rng, PCG_INT64 delta) {
    rng->state = pcg__advance_lcg_64(rng->state, delta, 6364136223846793005ULL, rng->inc);
}

PCG__DECLR PCG_INT32 pcg_random_in(PCG* rng, PCG_INT32 lower, PCG_INT32 upper) {
	PCG_INT32 bound = upper - lower + 1;
    // To avoid bias, we need to make the range of the RNG a multiple of
    // bound, which we do by dropping output less than a threshold.
    // A naive scheme to calculate the threshold would be to do
    //
    //     PCG_INT32 threshold = 0x100000000ull % bound;
    //
    // but 64-bit div/mod is slower than 32-bit div/mod (especially on
    // 32-bit platforms).  In essence, we do
    //
    //     PCG_INT32 threshold = (0x100000000ull-bound) % bound;
    //
    // because this version will calculate the same modulus, but the LHS
    // value is less than 2^32.

    PCG_INT32 threshold = ((PCG_INT32)(-(int32_t)bound)) % bound;

    // Uniformity guarantees that this loop will terminate.  In practice, it
    // should usually terminate quickly; on average (assuming all bounds are
    // equally likely), 82.25% of the time, we can expect it to require just
    // one iteration.  In the worst case, someone passes a bound of 2^31 + 1
    // (i.e., 2147483649), which invalidates almost 50% of the range.  In
    // practice, bounds are typically small and only a tiny amount of the range
    // is eliminated.
    for (;;) {
        PCG_INT32 r = pcg_random(rng);
        if (r >= threshold)
            return (r % bound) + lower;
    }
}

PCG__DECLR PCG_INT32 pcg_randomf(PCGF* state) {
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	PCG_INT32 x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

#endif

#ifdef __cplusplus
}
#endif
