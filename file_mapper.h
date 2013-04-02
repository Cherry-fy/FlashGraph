#ifndef __FILE_BLOCK_MAPPER_H__
#define __FILE_BLOCK_MAPPER_H__

#include <vector>
#include <string>

#include "common.h"
#include "parameters.h"

struct block_identifier
{
	int idx;
	off_t off;
};

class file_mapper
{
	std::vector<file_info> files;
protected:
	const std::vector<file_info> &get_files() const {
		return files;
	}
public:
	file_mapper(const std::vector<file_info> &files) {
		this->files = files;
	}

	const std::string &get_file_name(int idx) const {
		return files[idx].name;
	}

	int get_file_node_id(int idx) const {
		return files[idx].node_id;
	}

	int get_num_files() const {
		return (int) files.size();
	}

	virtual void map(off_t, struct block_identifier &) = 0;
	virtual int map2file(off_t) = 0;

	virtual file_mapper *clone() = 0;

	// size in bytes
	virtual int cycle_size() const = 0;
	virtual int cycle_size_in_bucket(int) const = 0;
};

class RAID0_mapper: public file_mapper
{
public:
	RAID0_mapper(const std::vector<file_info> &files): file_mapper(files) {
	}

	virtual void map(off_t off, struct block_identifier &bid) {
		int idx_in_block = off % STRIPE_BLOCK_SIZE;
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		bid.idx = (int) (block_idx % get_num_files());
		bid.off = block_idx / get_num_files() * STRIPE_BLOCK_SIZE
			+ idx_in_block;
	}

	virtual int map2file(off_t off) {
		return (int) ((off / STRIPE_BLOCK_SIZE) % get_num_files());
	}

	virtual file_mapper *clone() {
		return new RAID0_mapper(get_files());
	}

	virtual int cycle_size() const {
		return PAGE_SIZE * STRIPE_BLOCK_SIZE * get_num_files();
	}

	virtual int cycle_size_in_bucket(int) const {
		return PAGE_SIZE * STRIPE_BLOCK_SIZE;
	}
};

class RAID5_mapper: public RAID0_mapper
{
public:
	RAID5_mapper(const std::vector<file_info> &files): RAID0_mapper(
			files) {
	}

	virtual void map(off_t off, struct block_identifier &bid) {
		int idx_in_block = off % STRIPE_BLOCK_SIZE;
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		bid.idx = (int) (block_idx % get_num_files());
		bid.off = block_idx / get_num_files() * STRIPE_BLOCK_SIZE
			+ idx_in_block;
		int shift = (int) ((block_idx / get_num_files()) % get_num_files());
		bid.idx = (bid.idx + shift) % get_num_files();
	}

	virtual int map2file(off_t off) {
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		int shift = (int) ((block_idx / get_num_files()) % get_num_files());
		return (int) ((block_idx % get_num_files() + shift) % get_num_files());
	}

	virtual file_mapper *clone() {
		return new RAID5_mapper(get_files());
	}

	virtual int cycle_size() const {
		return PAGE_SIZE * STRIPE_BLOCK_SIZE * get_num_files();
	}

	virtual int cycle_size_in_bucket(int) const {
		return PAGE_SIZE * STRIPE_BLOCK_SIZE;
	}
};

class hash_mapper: public file_mapper
{
	static const int CONST_A = FILE_CONST_A;
	static const int CONST_P = FILE_CONST_P;
	const int P_MOD_N;
public:
	hash_mapper(
			const std::vector<file_info> &files): file_mapper(
				files), P_MOD_N(CONST_P % files.size()) {
	}

	virtual void map(off_t off, struct block_identifier &bid) {
		int idx_in_block = off % STRIPE_BLOCK_SIZE;
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		off_t p_idx = (CONST_A * block_idx) % CONST_P;
		bid.idx = p_idx % get_num_files();
		off_t cycle_idx = block_idx / CONST_P;
		int cycle_len_in_bucket = cycle_size_in_bucket(bid.idx);
		// length of all previous cycles
		bid.off = (cycle_idx * cycle_len_in_bucket
				// location in the current cycle
				+ p_idx / get_num_files())
			* STRIPE_BLOCK_SIZE + idx_in_block;
	}

	virtual int map2file(off_t off) {
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		off_t p_idx = (CONST_A * block_idx) % CONST_P;
		return p_idx % get_num_files();
	}

	virtual file_mapper *clone() {
		return new hash_mapper(get_files());
	}

	virtual int cycle_size() const {
		return CONST_P * STRIPE_BLOCK_SIZE * PAGE_SIZE;
	}

	virtual int cycle_size_in_bucket(int idx) const {
		return idx < P_MOD_N ? (CONST_P / get_num_files()
				+ 1) : (CONST_P / get_num_files());
	}
};

#endif