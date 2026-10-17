// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <errno.h>
#include <rt3000_b_bootargs.h>
#include <libfdt.h>

#define RT3_BOOTARGS_MAX_PARTS 64
#define RT3_BOOTARGS_MAX_NAME  RT3_BOOTARGS_LABEL_MAX

struct rt3_mtd_extent {
	u64 offset;
	u64 size;
	char name[RT3_BOOTARGS_MAX_NAME];
};

static int rt3_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int rt3_name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':';
}

static int rt3_hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Match U-Boot/Linux base-0 parsing; accept K/M/G size suffixes. */
static int rt3_parse_number(const char **cursor, const char *end, u64 *value)
{
	const char *p = *cursor;
	u64 n = 0, multiplier = 1;
	int digit, have_digit = 0, base = 10;

	if (p + 2 <= end && p[0] == '0' &&
	    (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		base = 16;
	} else if (p < end && *p == '0') {
		base = 8;
	}
	while (p < end && (digit = rt3_hex_value(*p)) >= 0 && digit < base) {
		if (n > (~(u64)0 - (u64)digit) / base)
			return -ERANGE;
		n = n * base + digit;
		have_digit = 1;
		p++;
	}
	if (!have_digit)
		return -EINVAL;
	if (p < end) {
		switch (*p) {
		case 'k': case 'K': multiplier = 1ULL << 10; p++; break;
		case 'm': case 'M': multiplier = 1ULL << 20; p++; break;
		case 'g': case 'G': multiplier = 1ULL << 30; p++; break;
		default: break;
		}
	}
	if (n > (~(u64)0) / multiplier)
		return -ERANGE;
	*value = n * multiplier;
	*cursor = p;
	return 0;
}

static int rt3_parse_device_segment(const char *begin, const char *end,
				    u64 target_offset, u64 target_size,
				    struct rt3_mtd_extent *parts,
				    unsigned int *part_count,
				    int *matched)
{
	const char *p = begin;
	u64 next_offset = 0;
	unsigned int count = 0, i;

	*matched = 0;
	while (p < end) {
		struct rt3_mtd_extent *part;
		const char *name_start;
		u64 size, offset;
		int ret;

		if (count >= RT3_BOOTARGS_MAX_PARTS)
			return -E2BIG;
		ret = rt3_parse_number(&p, end, &size);
		if (ret || !size)
			return ret ? ret : -EINVAL;
		offset = next_offset;
		if (p < end && *p == '@') {
			p++;
			ret = rt3_parse_number(&p, end, &offset);
			if (ret)
				return ret;
		}
		if (p >= end || *p++ != '(')
			return -EINVAL;
		name_start = p;
		while (p < end && *p != ')') {
			if (!rt3_name_char(*p))
				return -EINVAL;
			p++;
		}
		if (p == name_start || p >= end ||
		    (size_t)(p - name_start) >= sizeof(part->name))
			return -EINVAL;
		part = &parts[count];
		memcpy(part->name, name_start, p - name_start);
		part->name[p - name_start] = '\0';
		p++;
		if (p + 2 <= end && p[0] == 'r' && p[1] == 'o')
			p += 2;
		if (offset > (~(u64)0) - size)
			return -ERANGE;
		for (i = 0; i < count; i++) {
			u64 prev_end = parts[i].offset + parts[i].size;
			if (!strcmp(part->name, parts[i].name) ||
			    (offset < prev_end && parts[i].offset < offset + size))
				return -EEXIST;
		}
		part->offset = offset;
		part->size = size;
		if (offset == target_offset && size == target_size) {
			if (*matched)
				return -EEXIST;
			*matched = count + 1;
		}
		count++;
		next_offset = offset + size;
		if (p == end)
			break;
		if (*p++ != ',')
			return -EINVAL;
		if (p == end)
			return -EINVAL;
	}
	if (!count)
		return -EINVAL;
	*part_count = count;
	return 0;
}

/* Parse one mtdparts value. A mapping for any other controller is not enough
 * to identify the selected NAND extent, so explicit-but-unusable data fails.
 */
static int rt3_parse_mtdparts(const char *value, const char *end,
			      u64 target_offset, u64 target_size,
			      char *label, size_t label_size)
{
	const char *p = value, *segment_end;
	int qcom_seen = 0, found = 0;
	char matched_name[RT3_BOOTARGS_MAX_NAME] = { 0 };

	while (p < end) {
		const char *colon = p;
		while (colon < end && *colon != ':' && *colon != ';')
			colon++;
		if (colon == end || *colon != ':')
			return -EINVAL;
		segment_end = colon + 1;
		while (segment_end < end && *segment_end != ';')
			segment_end++;
		if ((size_t)(colon - p) == sizeof("qcom_nand.0") - 1 &&
		    !memcmp(p, "qcom_nand.0", sizeof("qcom_nand.0") - 1)) {
			struct rt3_mtd_extent parts[RT3_BOOTARGS_MAX_PARTS];
			unsigned int count = 0;
			int match_index, ret;
			if (qcom_seen++)
				return -EEXIST;
			ret = rt3_parse_device_segment(colon + 1, segment_end,
							 target_offset, target_size,
							 parts, &count, &match_index);
			if (ret)
				return ret;
			if (!match_index)
				return -ENOENT;
			strlcpy(matched_name, parts[match_index - 1].name,
				sizeof(matched_name));
			found = 1;
		}
		p = segment_end;
		if (p < end && *p == ';')
			p++;
	}
	if (!qcom_seen || !found)
		return -EINVAL;
	if (strlen(matched_name) + 1 > label_size)
		return -ENOSPC;
	strcpy(label, matched_name);
	return 0;
}

static int rt3_resolve_source(const char *args, size_t length,
			      u64 target_offset, u64 target_size,
			      char *label, size_t label_size, int *found)
{
	const char *p, *end;
	unsigned int seen = 0;

	*found = 0;
	if (!args && length)
		return -EINVAL;
	if (!args || !length)
		return 0;
	end = args + length;
	if (end[-1] == '\0')
		end--;
	if (memchr(args, '\0', end - args))
		return -EINVAL;
	p = args;
	while (p < end) {
		const char *token, *token_end;
		char candidate[RT3_BOOTARGS_MAX_NAME];
		int ret;
		while (p < end && rt3_space(*p)) p++;
		if (p == end) break;
		token = p;
		while (p < end && !rt3_space(*p)) p++;
		token_end = p;
		if ((size_t)(token_end - token) < sizeof("mtdparts=") - 1 ||
		    memcmp(token, "mtdparts=", sizeof("mtdparts=") - 1))
			continue;
		if (seen++)
			return -EEXIST;
		ret = rt3_parse_mtdparts(token + sizeof("mtdparts=") - 1,
					 token_end, target_offset, target_size,
					 candidate, sizeof(candidate));
		if (ret)
			return ret;
		if (*found && strcmp(label, candidate))
			return -EEXIST;
		if (!*found) {
			if (strlen(candidate) + 1 > label_size)
				return -ENOSPC;
			strcpy(label, candidate);
			*found = 1;
		}
	}
	return 0;
}

int rt3_bootargs_resolve_mtd_label(const char * const *args,
				  const size_t *lengths,
				  unsigned int count,
				  u64 offset, u64 size,
				  char *label, size_t label_size,
				  int *found)
{
	unsigned int i;
	int ret, source_found;

	char resolved[RT3_BOOTARGS_MAX_NAME] = { 0 };
	if (!found || !label || !label_size || (!args && count) ||
	    (!lengths && count) || !size || offset > (~(u64)0) - size)
		return -EINVAL;
	*found = 0;
	label[0] = '\0';
	for (i = 0; i < count; i++) {
		char candidate[RT3_BOOTARGS_MAX_NAME];
		ret = rt3_resolve_source(args[i], lengths[i], offset, size,
					 candidate, sizeof(candidate), &source_found);
		if (ret)
			return ret;
		if (source_found) {
			if (*found && strcmp(resolved, candidate))
				return -EEXIST;
			if (!*found) {
				if (strlen(candidate) + 1 > label_size)
					return -ENOSPC;
				strcpy(resolved, candidate);
				strcpy(label, candidate);
				*found = 1;
			}
		}
	}
	return 0;
}


/* Return whether a whitespace-delimited argument has the given key. */
static int rt3_has_key(const char *args, const char *key)
{
	const char *p;
	size_t key_len = strlen(key);

	if (!args)
		return 0;
	for (p = args; *p; ) {
		const char *start;

		while (rt3_space(*p))
			p++;
		start = p;
		while (*p && !rt3_space(*p))
			p++;
		if ((size_t)(p - start) >= key_len &&
		    !memcmp(start, key, key_len) && start[key_len - 1] == '=')
			return 1;
	}
	return 0;
}

/* Find one consistent ubi.mtd value in an argument string. */
static int rt3_find_ubi(const char *args, char *value, size_t value_size)
{
	const char *p;
	int found = 0;

	if (!args)
		return 0;
	for (p = args; *p; ) {
		const char *start;
		size_t value_len;

		while (rt3_space(*p))
			p++;
		start = p;
		while (*p && !rt3_space(*p))
			p++;
		if ((size_t)(p - start) < sizeof("ubi.mtd=") - 1 ||
		    memcmp(start, "ubi.mtd=", sizeof("ubi.mtd=") - 1))
			continue;
		value_len = p - start - (sizeof("ubi.mtd=") - 1);
		if (!value_len || value_len >= value_size)
			return -EINVAL;
		if (found && (strlen(value) != value_len ||
			      memcmp(value, start + sizeof("ubi.mtd=") - 1,
				     value_len)))
			return -EEXIST;
		memcpy(value, start + sizeof("ubi.mtd=") - 1, value_len);
		value[value_len] = '\0';
		found = 1;
	}
	return found;
}

/* Append tokens with optional filtering for superseded UBI/root arguments. */
static int rt3_append(char *output, size_t output_size, size_t *used,
		      const char *args, int strip_ubi, int strip_root)
{
	const char *p;

	if (!args)
		return 0;
	for (p = args; *p; ) {
		const char *start;
		size_t token_len;

		while (rt3_space(*p))
			p++;
		if (!*p)
			break;
		start = p;
		while (*p && !rt3_space(*p))
			p++;
		token_len = p - start;
		if (strip_ubi && token_len >= sizeof("ubi.mtd=") - 1 &&
		    !memcmp(start, "ubi.mtd=", sizeof("ubi.mtd=") - 1))
			continue;
		if (strip_root &&
		    ((token_len >= sizeof("root=") - 1 &&
		      !memcmp(start, "root=", sizeof("root=") - 1)) ||
		     (token_len >= sizeof("rootfstype=") - 1 &&
		      !memcmp(start, "rootfstype=", sizeof("rootfstype=") - 1))))
			continue;
		if (*used) {
			if (*used + 1 >= output_size)
				return -ENOSPC;
			output[(*used)++] = ' ';
		}
		if (token_len >= output_size - *used)
			return -ENOSPC;
		memcpy(output + *used, start, token_len);
		*used += token_len;
		output[*used] = '\0';
	}
	return 0;
}

int rt3_bootargs_compose(const char *base_args, const char *fit_args,
			 const char *fallback_args, const void *fdt,
			 size_t fdt_size, u64 offset, u64 size,
			 char *output, size_t output_size)
{
	const char *sources[RT3_BOOTARGS_MAX_SOURCES];
	const char *chosen_bootargs = NULL, *chosen_append = NULL;
	size_t lengths[RT3_BOOTARGS_MAX_SOURCES], used = 0;
	char label[RT3_BOOTARGS_LABEL_MAX];
	char ubi_bootargs[RT3_BOOTARGS_LABEL_MAX];
	char ubi_append[RT3_BOOTARGS_LABEL_MAX];
	char ubi_arg[RT3_BOOTARGS_LABEL_MAX + sizeof("ubi.mtd=")];
	int count = 0, found = 0, chosen, len, ret;
	int root_present, ubi_bootargs_found, ubi_append_found;

	if (!output || !output_size || !size)
		return -EINVAL;
	output[0] = '\0';
	if (fit_args) {
		sources[count] = fit_args;
		lengths[count++] = strlen(fit_args);
	}
	if (fdt) {
		if (fdt_size < sizeof(struct fdt_header) ||
		    fdt_check_header(fdt) || fdt_totalsize(fdt) > fdt_size)
			return -EINVAL;
		chosen = fdt_path_offset(fdt, "/chosen");
		if (chosen >= 0) {
			chosen_bootargs = fdt_getprop(fdt, chosen, "bootargs", &len);
			if (chosen_bootargs &&
			    (len < 1 || chosen_bootargs[len - 1] != '\0' ||
			     memchr(chosen_bootargs, '\0', len - 1)))
				return -EINVAL;
			chosen_append = fdt_getprop(fdt, chosen, "bootargs-append",
						    &len);
			if (chosen_append &&
			    (len < 1 || chosen_append[len - 1] != '\0' ||
			     memchr(chosen_append, '\0', len - 1)))
				return -EINVAL;
			if (chosen_bootargs) {
				sources[count] = chosen_bootargs;
				lengths[count++] = strlen(chosen_bootargs);
			}
			if (chosen_append) {
				sources[count] = chosen_append;
				lengths[count++] = strlen(chosen_append);
			}
		} else if (chosen != -FDT_ERR_NOTFOUND) {
			return -EINVAL;
		}
	} else if (fdt_size) {
		return -EINVAL;
	}

	ret = rt3_bootargs_resolve_mtd_label(sources, lengths, count, offset,
					     size, label, sizeof(label), &found);
	if (ret)
		return ret;
	root_present = rt3_has_key(base_args, "root=") ||
		       rt3_has_key(fit_args, "root=") ||
		       rt3_has_key(chosen_bootargs, "root=") ||
		       rt3_has_key(chosen_append, "root=");

	/* Keep legacy behavior when no explicit physical-to-Linux map exists. */
	if (!found) {
		ret = rt3_append(output, output_size, &used, base_args, 0, 0);
		if (ret)
			return ret;
		if (fit_args && *fit_args)
			ret = rt3_append(output, output_size, &used, fit_args, 0, 0);
		else
			ret = rt3_append(output, output_size, &used,
					 fallback_args, 0, 0);
		if (!ret)
			ret = rt3_append(output, output_size, &used, "rootwait", 0, 0);
		return ret;
	}

	/* FIT base/append arguments can contain stale environment UBI names. */
	ret = rt3_append(output, output_size, &used, base_args, 1, 0);
	if (!ret)
		ret = rt3_append(output, output_size, &used, fit_args, 1, 0);
	if (ret)
		return ret;

	/* Kernel FDT UBI names cannot be removed; accept only the mapped name. */
	ubi_bootargs_found = rt3_find_ubi(chosen_bootargs, ubi_bootargs,
					   sizeof(ubi_bootargs));
	if (ubi_bootargs_found < 0)
		return ubi_bootargs_found;
	ubi_append_found = rt3_find_ubi(chosen_append, ubi_append,
					 sizeof(ubi_append));
	if (ubi_append_found < 0)
		return ubi_append_found;
	if ((ubi_bootargs_found && strcmp(label, ubi_bootargs)) ||
	    (ubi_append_found && strcmp(label, ubi_append)))
		return -EEXIST;
	if (!ubi_bootargs_found && !ubi_append_found) {
		snprintf(ubi_arg, sizeof(ubi_arg), "ubi.mtd=%s", label);
		ret = rt3_append(output, output_size, &used, ubi_arg, 0, 0);
		if (ret)
			return ret;
	}
	ret = rt3_append(output, output_size, &used, fallback_args, 1,
			 root_present);
	if (!ret)
		ret = rt3_append(output, output_size, &used, "rootwait", 0, 0);
	return ret;
}
