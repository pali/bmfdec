/*
    bmfparse.c - Parse binary MOF file (BMF)
    Copyright (C) 2017  Pali Rohár <pali.rohar@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#define process_data bmfdec_process_data
#include "bmfdec.c"
#undef process_data

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define error(str) do { fprintf(stderr, "error %s at %s:%d\n", str, __func__, __LINE__); exit(1); } while (0)

#define check_sum(a, b, sum) (UINT32_MAX - (uint32_t)(a) >= (uint32_t)(b) && (uint32_t)(a)+(uint32_t)(b) <= (uint32_t)(sum))

enum mof_qualifier_type {
  MOF_QUALIFIER_UNKNOWN,
  MOF_QUALIFIER_BOOLEAN,
  MOF_QUALIFIER_SINT32,
  MOF_QUALIFIER_STRING,
};

enum mof_variable_type {
  MOF_VARIABLE_UNKNOWN,
  MOF_VARIABLE_BASIC,
  MOF_VARIABLE_OBJECT,
  MOF_VARIABLE_BASIC_ARRAY,
  MOF_VARIABLE_OBJECT_ARRAY,
};

enum mof_basic_type {
  MOF_BASIC_TYPE_UNKNOWN,
  MOF_BASIC_TYPE_STRING,
  MOF_BASIC_TYPE_SINT32,
  MOF_BASIC_TYPE_UINT32,
  MOF_BASIC_TYPE_SINT16,
  MOF_BASIC_TYPE_UINT16,
  MOF_BASIC_TYPE_SINT64,
  MOF_BASIC_TYPE_UINT64,
  MOF_BASIC_TYPE_SINT8,
  MOF_BASIC_TYPE_UINT8,
  MOF_BASIC_TYPE_DATETIME,
  MOF_BASIC_TYPE_BOOLEAN,
};

enum mof_parameter_direction {
  MOF_PARAMETER_UNKNOWN,
  MOF_PARAMETER_IN,
  MOF_PARAMETER_OUT,
  MOF_PARAMETER_IN_OUT,
};

struct mof_qualifier {
  enum mof_qualifier_type type;
  char *name;
  uint8_t tosubclass;
  union {
    uint8_t boolean;
    int32_t sint32;
    char *string;
  } value;
};

struct mof_variable {
  uint32_t qualifiers_count;
  struct mof_qualifier *qualifiers;
  char *name;
  enum mof_variable_type variable_type;
  union {
    enum mof_basic_type basic;
    char *object;
  } type;
  int32_t array;
};

struct mof_method {
  uint32_t qualifiers_count;
  struct mof_qualifier *qualifiers;
  char *name;
  uint32_t parameters_count;
  struct mof_variable *parameters;
  enum mof_parameter_direction *parameters_direction;
  struct mof_variable return_value;
};

struct mof_class {
  char *name;
  char *namespace;
  char *superclassname;
  int32_t classflags;
  uint32_t qualifiers_count;
  struct mof_qualifier *qualifiers;
  uint32_t variables_count;
  struct mof_variable *variables;
  uint32_t methods_count;
  struct mof_method *methods;
};

struct mof_classes {
  uint32_t count;
  struct mof_class *classes;
};

static char *parse_string(char *buf, uint32_t size) {
  uint16_t *buf2 = (uint16_t *)buf;
  if (size % 2 != 0) error("Invalid size");
  char *out = malloc(size+1);
  if (!out) error("malloc failed");
  uint32_t i, j;
  for (i=0, j=0; i<size/2; ++i) {
    if (buf2[i] == 0) {
      break;
    } else if (buf2[i] < 0x80) {
      out[j++] = buf2[i];
    } else if (buf2[i] < 0x800) {
      out[j++] = 0xC0 | (buf2[i] >> 6);
      out[j++] = 0x80 | (buf2[i] & 0x3F);
    } else if (buf2[i] >= 0xD800 && buf2[i] <= 0xDBFF && i+1 < size/2 && buf2[i+1] >= 0xDC00 && buf2[i+1] <= 0xDFFF) {
      uint32_t c = 0x10000 + ((buf2[i] - 0xD800) << 10) + (buf2[i+1] - 0xDC00);
      ++i;
      out[j++] = 0xF0 | (c >> 18);
      out[j++] = 0x80 | ((c >> 12) & 0x3F);
      out[j++] = 0x80 | ((c >> 6) & 0x3F);
      out[j++] = 0x80 | (c & 0x3F);
    } else {
      out[j++] = 0xE0 | (buf2[i] >> 12);
      out[j++] = 0x80 | ((buf2[i] >> 6) & 0x3F);
      out[j++] = 0x80 | (buf2[i] & 0x3F);
    }
  }
  out[j] = 0;
  return out;
}

static char to_ascii(char c) {
  if (c >= 32 && c <= 126)
    return c;
  return '.';
}

static void dump_bytes(char *buf, uint32_t size) {
  uint32_t i, ascii_cnt = 0;
  char ascii[17] = { 0, };
  for (i=0; i<size; i++) {
    if (i % 16 == 0) {
      if (i != 0) {
        fprintf(stderr, "  |%s|\n", ascii);
        ascii[0] = 0;
        ascii_cnt = 0;
      }
      fprintf(stderr, "%04X:", (unsigned int)i);
    }
    fprintf(stderr, " %02X", buf[i] & 0xFF);
    ascii[ascii_cnt] = to_ascii(buf[i]);
    ascii[ascii_cnt + 1] = 0;
    ascii_cnt++;
  }
  if (ascii[0]) {
    if (size % 16)
      for (i=0; i<16-(size%16); i++)
        fprintf(stderr, "   ");
    fprintf(stderr, "  |%s|\n", ascii);
  }
}

static struct mof_qualifier parse_qualifier_boolean(char *buf, uint32_t size, uint32_t val) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  if (val != 0 && val != 0xFFFF) error("Invalid boolean");
  out.type = MOF_QUALIFIER_BOOLEAN;
  out.name = parse_string(buf, size);
  out.value.boolean = val ? 1 : 0;
  return out;
}

static struct mof_qualifier parse_qualifier_sint32(char *buf, uint32_t size, int32_t val) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  out.type = MOF_QUALIFIER_SINT32;
  out.name = parse_string(buf, size);
  out.value.sint32 = val;
  return out;
}

static struct mof_qualifier parse_qualifier_string(char *buf, uint32_t size, char *buf2, uint32_t size2) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  out.type = MOF_QUALIFIER_STRING;
  out.name = parse_string(buf, size);
  out.value.string = parse_string(buf2, size2);
  return out;
}

static struct mof_qualifier parse_qualifier(char *buf, uint32_t size, uint32_t offset) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 16) error("Invalid size");
  uint32_t type = buf2[1];
  uint32_t len = buf2[3];
  if (!check_sum(16, len, size)) error("Invalid size");
  switch (type) {
  case 0x0B:
    if (check_sum(16+4+1, len, size)) error("Invalid size");
    out = parse_qualifier_boolean(buf+16, len, !check_sum(16+4, len, size) ? 0xFFFF : *((uint32_t *)(buf+16+len)));
    break;
  case 0x03:
    if (!check_sum(16+4, len, size)) error("Invalid size");
    out = parse_qualifier_sint32(buf+16, len, *((int32_t *)(buf+16+len)));
    break;
  case 0x08:
    out = parse_qualifier_string(buf+16, len, buf+16+len, size-len-16);
    break;
  case 0x2008:
    fprintf(stderr, "Warning: ValueMap and Values qualifiers are not supported yet\n");
    break;
  default:
    fprintf(stderr, "Warning: Unknown qualifier type 0x%x\n", type);
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf+16, len);
    if (len+16 < size) {
      fprintf(stderr, "...continue...\n");
      dump_bytes(buf+16+len, size-len-16);
    }
    break;
  }
  if (offset) {
    uint32_t i;
    uint32_t olen = ((uint32_t *)(buf-offset))[1];
    uint32_t count = ((uint32_t *)(buf-offset+olen+16))[0];
    for (i=0; i<count; ++i) {
      uint32_t *offset_addr = (uint32_t *)(buf-offset+olen+16+4) + 2*i;
      if (*offset_addr != offset)
        continue;
      *offset_addr = 0;
      uint32_t type2 = ((uint32_t *)(buf-offset+olen+16+4))[2*i+1];
      switch (type2) {
      case 0x01:
        if (out.type != MOF_QUALIFIER_BOOLEAN || strcasecmp(out.name, "Dynamic") != 0) error("qualifier type in second part does not match");
        break;
      case 0x02:
        out.tosubclass = 1;
        break;
      case 0x03:
        if (out.type != MOF_QUALIFIER_STRING || strcmp(out.name, "CIMTYPE") != 0) error("qualifier type in second part does not match");
        break;
      case 0x11:
        if (out.type != MOF_QUALIFIER_SINT32 || strcmp(out.name, "ID") != 0) error("qualifier type in second part does not match");
        break;
      default:
        fprintf(stderr, "Warning: Unknown qualifier type in second part 0x%x for %s\n", type2, out.name);
        break;
      }
    }
  }
  return out;
}

static struct mof_variable parse_class_variable(char *buf, uint32_t size, uint32_t offset) {
  struct mof_variable out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 20) error("Invalid size");
  uint32_t type = buf2[1];
  int is_array;
  switch (type >> 8) {
  case 0x00:
    is_array = 0;
    break;
  case 0x20:
    is_array = 1;
    break;
  default:
    fprintf(stderr, "Warning: unknown variable type 0x%x\n", type);
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf, size);
    return out;
  }
  switch (type & 0xFF) {
  case 0x02:
    out.type.basic = MOF_BASIC_TYPE_SINT16;
    break;
  case 0x03:
    out.type.basic = MOF_BASIC_TYPE_SINT32;
    break;
  case 0x08:
    out.type.basic = MOF_BASIC_TYPE_STRING;
    break;
  case 0x0B:
    out.type.basic = MOF_BASIC_TYPE_BOOLEAN;
    break;
  case 0x10:
    out.type.basic = MOF_BASIC_TYPE_SINT8;
    break;
  case 0x11:
    out.type.basic = MOF_BASIC_TYPE_UINT8;
    break;
  case 0x12:
    out.type.basic = MOF_BASIC_TYPE_UINT16;
    break;
  case 0x13:
    out.type.basic = MOF_BASIC_TYPE_UINT32;
    break;
  case 0x14:
    out.type.basic = MOF_BASIC_TYPE_SINT64;
    break;
  case 0x15:
    out.type.basic = MOF_BASIC_TYPE_UINT64;
    break;
  case 0x65:
    out.type.basic = MOF_BASIC_TYPE_DATETIME;
    break;
  case 0x0D:
    /* object */
    break;
  default:
    fprintf(stderr, "Warning: unknown variable type 0x%x\n", type);
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf, size);
    return out;
  }
  if ((type & 0xFF) == 0x0D) {
    if (is_array)
      out.variable_type = MOF_VARIABLE_OBJECT_ARRAY;
    else
      out.variable_type = MOF_VARIABLE_OBJECT;
  } else {
    if (is_array)
      out.variable_type = MOF_VARIABLE_BASIC_ARRAY;
    else
      out.variable_type = MOF_VARIABLE_BASIC;
  }
  if (buf2[2] != 0x0) error("Invalid unknown");
  uint32_t len = buf2[4];
  if (!check_sum(20, len, size)) error("Invalid size");
  uint32_t slen = buf2[3];
  if (slen != 0xFFFFFFFF) {
    if (!check_sum(20, slen, size) || slen > len) error("Invalid size");
    out.name = parse_string(buf+20, slen);
    fprintf(stderr, "Warning: Variable value is not supported yet\n");
    dump_bytes(buf+20+slen, len-slen);
  } else {
    out.name = parse_string(buf+20, len);
  }
  if (!check_sum(20+8, len, size)) error("Invalid size");
  buf2 = (uint32_t *)(buf+20+len);
  uint32_t len1 = buf2[0];
  if (size < 20 || !check_sum(len, len1, size-20)) error("Invalid size");
  uint32_t count = buf2[1];
  uint32_t i;
  char *tmp = buf+20+len+8;
  out.qualifiers = calloc(count, sizeof(*out.qualifiers));
  if (!out.qualifiers) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (tmp-buf <= 20+8 || tmp-buf >= UINT32_MAX) error("Invalid size");
    if (!check_sum(len, len1, UINT32_MAX) || !check_sum(20+8, len+len1, UINT32_MAX) || !check_sum(tmp-buf, 4, 20+len+8+len1)) error("Invalid size"); /* if (tmp+4 > buf+20+len+8+len1) */
    uint32_t len2 = ((uint32_t *)tmp)[0];
    if (len2 == 0 || len2 >= len1) error("Invalid size");
    if (!check_sum(tmp-buf, len2, 20+len+8+len1)) error("Invalid size"); /* if (tmp+len2 > buf+20+len+8+len1) */
    out.qualifiers[out.qualifiers_count] = parse_qualifier(tmp, len2, offset ? offset+tmp-buf : 0);
    if (out.qualifiers[out.qualifiers_count].name) {
      if (out.qualifiers[out.qualifiers_count].type == MOF_QUALIFIER_STRING && strcmp(out.qualifiers[out.qualifiers_count].name, "CIMTYPE") == 0) {
        if (out.variable_type == MOF_VARIABLE_OBJECT || out.variable_type == MOF_VARIABLE_OBJECT_ARRAY) {
          if (strncmp(out.qualifiers[out.qualifiers_count].value.string, "object:", strlen("object:")) != 0)
            error("object without 'object:' in CIMTYPE");
          out.type.object = strdup(out.qualifiers[out.qualifiers_count].value.string + strlen("object:"));
          free(out.qualifiers[out.qualifiers_count].name);
          free(out.qualifiers[out.qualifiers_count].value.string);
        } else {
          char *strtype = out.qualifiers[out.qualifiers_count].value.string;
          enum mof_basic_type basic_type;
          if (strcmp(strtype, "String") == 0 || strcmp(strtype, "string") == 0)
            basic_type = MOF_BASIC_TYPE_STRING;
          else if (strcmp(strtype, "sint32") == 0)
            basic_type = MOF_BASIC_TYPE_SINT32;
          else if (strcmp(strtype, "uint32") == 0)
            basic_type = MOF_BASIC_TYPE_UINT32;
          else if (strcmp(strtype, "sint16") == 0)
            basic_type = MOF_BASIC_TYPE_SINT16;
          else if (strcmp(strtype, "uint16") == 0)
            basic_type = MOF_BASIC_TYPE_UINT16;
          else if (strcmp(strtype, "sint64") == 0)
            basic_type = MOF_BASIC_TYPE_SINT64;
          else if (strcmp(strtype, "uint64") == 0)
            basic_type = MOF_BASIC_TYPE_UINT64;
          else if (strcmp(strtype, "sint8") == 0)
            basic_type = MOF_BASIC_TYPE_SINT8;
          else if (strcmp(strtype, "uint8") == 0)
            basic_type = MOF_BASIC_TYPE_UINT8;
          else if (strcmp(strtype, "Datetime") == 0 || strcmp(strtype, "datetime") == 0)
            basic_type = MOF_BASIC_TYPE_DATETIME;
          else if (strcmp(strtype, "Boolean") == 0 || strcmp(strtype, "boolean") == 0)
            basic_type = MOF_BASIC_TYPE_BOOLEAN;
          else
            error("unknown basic type");
          free(out.qualifiers[out.qualifiers_count].value.string);
          free(out.qualifiers[out.qualifiers_count].name);
          if (basic_type != out.type.basic) error("basic type does not match");
        }
      } else if (out.qualifiers[out.qualifiers_count].type == MOF_QUALIFIER_SINT32 && strcmp(out.qualifiers[out.qualifiers_count].name, "MAX") == 0 && is_array) {
        out.array = out.qualifiers[out.qualifiers_count].value.sint32;
        free(out.qualifiers[out.qualifiers_count].name);
      } else {
        out.qualifiers_count++;
      }
    }
    tmp += len2;
  }
  if (tmp != buf+size) error("Buffer not processed");
  return out;
}

static struct mof_class parse_class_data(char *buf, uint32_t size, uint32_t size1, int with_qualifiers, uint32_t offset);
static void free_classes(struct mof_class *classes, uint32_t count);

static int cmp_qualifiers(struct mof_qualifier *a, struct mof_qualifier *b) {
  if (strcmp(a->name, b->name) != 0 || a->type != b->type)
    return 1;
  switch (a->type) {
  case MOF_QUALIFIER_BOOLEAN:
    return (a->value.boolean != b->value.boolean) ? 1 : 0;
  case MOF_QUALIFIER_SINT32:
    return (a->value.sint32 != b->value.sint32) ? 1 : 0;
  case MOF_QUALIFIER_STRING:
    return strcmp(a->value.string, b->value.string) ? 1 : 0;
  default:
    return 1;
  }
}

static int cmp_variables(struct mof_variable *a, struct mof_variable *b) {
  if (strcmp(a->name, b->name) != 0 || a->variable_type != b->variable_type)
    return 1;
  if ((a->variable_type == MOF_VARIABLE_BASIC_ARRAY || a->variable_type == MOF_VARIABLE_OBJECT_ARRAY) && a->array != b->array)
    return 1;
  switch (a->variable_type) {
  case MOF_VARIABLE_BASIC:
  case MOF_VARIABLE_BASIC_ARRAY:
    return (a->type.basic != b->type.basic) ? 1 : 0;
  case MOF_VARIABLE_OBJECT:
  case MOF_VARIABLE_OBJECT_ARRAY:
    return strcmp(a->type.object, b->type.object) ? 1 : 0;
  default:
    return 1;
  }
}

static void parse_class_method_parameters(char *buf, uint32_t size, struct mof_method *out, uint32_t offset) {
  struct mof_class *parameters;
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 16) error("Invalid size");
  if (buf2[1] != 0x1) error("Invalid unknown");
  uint32_t count = buf2[2];
  uint32_t len = buf2[3];
  if (len == 0 || !check_sum(12, len, size)) error("Invalid size");
  if (len+12 != size) error("Invalid size?");
  uint32_t i;
  char *tmp = buf+16;
  parameters = calloc(count, sizeof(*parameters));
  if (!parameters) error("calloc failed");
  for (i=0; i<count; ++i) {
    buf2 = (uint32_t *)tmp;
    if (tmp-buf >= UINT32_MAX) error("Invalid size");
    if (!check_sum(4, tmp-buf, len)) error("Invalid size");
    uint32_t len1 = buf2[0];
    if (!check_sum(tmp-buf, len1, 16+len)) error("Invalid size");
    if (len1 < 20) error("Invalid size");
    if (buf2[1] != 0xFFFFFFFF) error("Invalid unknown");
    if (buf2[2] != 0x0) error("Invalid unknown");
    uint32_t len2 = buf2[3];
    if (len2 >= len || !check_sum(tmp-buf, 20-16, len-len2)) error("Invalid size"); /* if (tmp+len2+20 > buf+16+len) */
    if (buf2[4] != 0x1) error("Invalid unknown");
    parameters[i] = parse_class_data(tmp+20, len2, len2, 0, offset ? offset+tmp+20-buf : 0);
    if (strcmp(parameters[i].name, "__PARAMETERS") != 0) error("Invalid parameters class name");
    tmp += len1;
  }
  uint32_t variables_count = 0;
  for (i=0; i<count; ++i) {
    variables_count += parameters[i].variables_count;
  }
  uint8_t *parameters_map = calloc(variables_count, sizeof(uint8_t));
  if (!parameters_map) error("calloc failed");
  uint32_t j, k;
  for (i=0; i<count; ++i) {
    for (j=0; j<parameters[i].variables_count; ++j) {
      int processed = 0;
      for (k=0; k<parameters[i].variables[j].qualifiers_count; ++k) {
        if (parameters[i].variables[j].qualifiers[k].type != MOF_QUALIFIER_SINT32)
          continue;
        if (strcmp(parameters[i].variables[j].qualifiers[k].name, "ID") != 0)
          continue;
        if (processed) error("parameter has more IDs");
        int32_t id = parameters[i].variables[j].qualifiers[k].value.sint32;
        if (id < 0 || (uint32_t)id >= variables_count) error("invalid parameter ID");
        parameters_map[id] = 1;
        processed = 1;
      }
      int return_value = (strcmp(parameters[i].variables[j].name, "ReturnValue") == 0) ? 1 : 0;
      if (!(processed ^ return_value)) error("variable is not parameter nor return value");
    }
  }
  uint32_t parameters_count = (variables_count && parameters_map[0]) ? 1 : 0;
  for (i=1; i<variables_count; ++i) {
    if (parameters_map[i]) {
      if (!parameters_map[i-1]) error("some parameters are missing");
      parameters_count = i+1;
    }
  }
  out->parameters_count = parameters_count;
  out->parameters = calloc(parameters_count, sizeof(*out->parameters));
  out->parameters_direction = calloc(parameters_count, sizeof(*out->parameters_direction));
  if (!out->parameters) error("calloc failed");
  int has_return_value = 0;
  for (i=0; i<count; ++i) {
    for (j=0; j<parameters[i].variables_count; ++j) {
      int32_t id = -1;
      struct mof_variable variable = parameters[i].variables[j];
      for (k=0; k<variable.qualifiers_count; ++k) {
        if (variable.qualifiers[k].type != MOF_QUALIFIER_SINT32)
          continue;
        if (strcmp(variable.qualifiers[k].name, "ID") != 0)
          continue;
        id = variable.qualifiers[k].value.sint32;
        break;
      }
      if (id != -1) {
        if (parameters_map[id] == 2) {
          if (cmp_variables(&out->parameters[id], &variable) != 0) error("two variables at same position");
          out->parameters[id].qualifiers = realloc(out->parameters[id].qualifiers, (out->parameters[id].qualifiers_count+variable.qualifiers_count-1)*sizeof(*out->parameters[id].qualifiers));
          if (!out->parameters[id].qualifiers) error("realloc failed");
        } else {
          out->parameters[id] = variable;
          out->parameters[id].qualifiers_count = 0;
          out->parameters[id].qualifiers = calloc(variable.qualifiers_count-1, sizeof(*out->parameters[id].qualifiers));
          parameters_map[id] = 2;
          memset(&parameters[i].variables[j], 0, sizeof(parameters[i].variables[j]));
          parameters[i].variables[j].qualifiers_count = variable.qualifiers_count;
          parameters[i].variables[j].qualifiers = variable.qualifiers;
        }
        for (k=0; k<variable.qualifiers_count; ++k) {
          if (variable.qualifiers[k].type == MOF_QUALIFIER_SINT32 &&
              strcmp(variable.qualifiers[k].name, "ID") == 0)
            continue;
          if (variable.qualifiers[k].type == MOF_QUALIFIER_BOOLEAN) {
            if (strcmp(variable.qualifiers[k].name, "in") == 0) {
              if (!out->parameters_direction[id])
                out->parameters_direction[id] = MOF_PARAMETER_IN;
              else
                out->parameters_direction[id] = MOF_PARAMETER_IN_OUT;
              continue;
            } else if (strcmp(variable.qualifiers[k].name, "out") == 0) {
              if (!out->parameters_direction[id])
                out->parameters_direction[id] = MOF_PARAMETER_OUT;
              else
                out->parameters_direction[id] = MOF_PARAMETER_IN_OUT;
              continue;
            }
          }
          int skip = 0;
          uint32_t l;
          for (l=0; l<out->parameters[id].qualifiers_count; ++l) {
            if (cmp_qualifiers(&out->parameters[id].qualifiers[l], &variable.qualifiers[k]) == 0) {
              skip = 1;
              break;
            }
          }
          if (skip)
            continue;
          out->parameters[id].qualifiers[out->parameters[id].qualifiers_count++] = variable.qualifiers[k];
          memset(&parameters[i].variables[j].qualifiers[k], 0, sizeof(parameters[i].variables[j].qualifiers[k]));
        }
      } else if (strcmp(variable.name, "ReturnValue") == 0) {
        if (has_return_value) error("multiple return values");
        out->return_value = variable;
        has_return_value = 1;
        memset(&parameters[i].variables[j], 0, sizeof(parameters[i].variables[j]));
      } else {
        error("variable is not parameter nor return value");
      }
    }
  }
  free(parameters_map);
  free_classes(parameters, count);
  for (i=0; i<out->parameters_count; ++i) {
    if (out->parameters_direction[i] != MOF_PARAMETER_IN &&
        out->parameters_direction[i] != MOF_PARAMETER_OUT &&
        out->parameters_direction[i] != MOF_PARAMETER_IN_OUT) error("parameter is not input nor output");
  }
}

static struct mof_method parse_class_method(char *buf, uint32_t size, uint32_t offset) {
  struct mof_method out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 20) error("Invalid size");
  if (buf2[1] != 0x00 && buf2[1] != 0x200D) {
    fprintf(stderr, "Warning: unknown method type 0x%x\n", ((uint32_t *)buf)[1]);
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf, size);
    return out;
  }
  if (buf2[2] != 0x0) error("Invalid unknown");
  uint32_t len = buf2[3];
  if (len == 0xFFFFFFFF)
    len = buf2[4];
  else {
    if (!check_sum(20, buf2[4], size) || buf2[4] < len) error("Invalid size");
    parse_class_method_parameters(buf+20+len, buf2[4]-len, &out, offset ? offset+20+len : 0);
  }
  if (!check_sum(20, len, size)) error("Invalid size");
  out.name = parse_string(buf+20, len);
  len = buf2[4];
  buf2 = (uint32_t *)(buf+20+len);
  uint32_t len1 = buf2[0];
  if (size < 20 || !check_sum(len, len1, size-20)) error("Invalid size");
  uint32_t count = buf2[1];
  uint32_t i;
  char *tmp = buf+20+len+8;
  out.qualifiers_count = count;
  out.qualifiers = calloc(count, sizeof(*out.qualifiers));
  if (!out.qualifiers) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (tmp-buf >= UINT32_MAX || !check_sum(20+8, len+len1, UINT32_MAX) || !check_sum(tmp-buf, 4, 20+len+8+len1)) error("Invalid size"); /* if (tmp+4 > buf+20+len+8+len1) */
    uint32_t len2 = ((uint32_t *)tmp)[0];
    if (len2 == 0 || !check_sum(tmp-buf, len2, 20+len+8+len1)) error("Invalid size"); /* if (tmp+len2 > buf+20+len+8+len1) */
    out.qualifiers[i] = parse_qualifier(tmp, len2, offset ? offset+tmp-buf : 0);
    tmp += len2;
  }
  if (tmp != buf+size) error("Buffer not processed");
  return out;
}

static void parse_class_property(char *buf, uint32_t size, struct mof_class *out) {
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 20) error("Invalid size");
  uint32_t len = buf2[0];
  if (len == 0 || size < len) error("Invalid size");
  if (buf2[2] != 0x0 || buf2[4] != 0xFFFFFFFF) error("Invalid unknown");
  uint32_t type = buf2[1];
  uint32_t slen = buf2[3];
  if (!check_sum(20, slen, size)) error("Invalid size");
  char *name = parse_string(buf+20, slen);
  if (type == 0x08) {
    char *value = parse_string(buf+20+slen, size-slen-20);
    if (strcmp(name, "__CLASS") == 0) {
      out->name = value;
    } else if (strcmp(name, "__NAMESPACE") == 0) {
      out->namespace = value;
    } else if (strcmp(name, "__SUPERCLASS") == 0) {
      out->superclassname = value;
    } else {
      fprintf(stderr, "Warning: Unknown class property name %s\n", name);
      free(value);
    }
  } else if (type == 0x03) {
    if (size-slen-20 != 4) error("Invalid size");
    int32_t value = *((int32_t *)(buf+20+slen));
    if (strcmp(name, "__CLASSFLAGS") == 0) {
      out->classflags = value;
    } else {
      fprintf(stderr, "Warning: Unknown class property name %s\n", name);
    }
  } else {
    fprintf(stderr, "Warning: Unknown class property type 0x%x for name %s\n", type, name);
  }
  free(name);
}

static struct mof_class parse_class_data(char *buf, uint32_t size, uint32_t size1, int with_qualifiers, uint32_t offset) {
  struct mof_class out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 8) error("Invalid size");
  uint32_t len1 = buf2[0];
  if (len1 > size) error("Invalid size");
  if (len1 != size1) error("Invalid size");
  uint32_t count1 = buf2[1];
  uint32_t i;
  char *tmp = buf + 8;
  if (with_qualifiers) {
    out.qualifiers_count = count1;
    out.qualifiers = calloc(count1, sizeof(*out.qualifiers));
    if (!out.qualifiers) error("calloc failed");
    for (i=0; i<count1; ++i) {
      if (tmp-buf >= UINT32_MAX || !check_sum(tmp-buf, 4, len1)) error("Invalid size");
      uint32_t len = ((uint32_t *)tmp)[0];
      if (len == 0 || !check_sum(tmp-buf, len, len1)) error("Invalid size");
      out.qualifiers[i] = parse_qualifier(tmp, len, offset ? offset+tmp-buf : 0);
      tmp += len;
    }
  } else {
    tmp = buf;
    count1 = 0;
    len1 = 0;
  }
  buf2 = (uint32_t *)tmp;
  uint32_t len2 = buf2[0];
  uint32_t count2 = buf2[1];
  if (!check_sum(len1, len2, size)) error("Invalid size");
  tmp += 8;
  out.variables = calloc(count2, sizeof(*out.variables));
  if (!out.variables) error("calloc failed");
  for (i=0; i<count2; ++i) {
    if (tmp-buf >= UINT32_MAX || !check_sum(len1, len2, UINT32_MAX)) error("Invalid size");
    if (!check_sum(tmp-buf, 4, len1+len2)) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || !check_sum(tmp-buf, len, len1+len2)) error("Invalid size");
    if (tmp+16 <= buf+len1+len2 && ((uint32_t *)tmp)[4] == 0xFFFFFFFF) {
      parse_class_property(tmp, len, &out);
    } else {
      out.variables[i] = parse_class_variable(tmp, len, offset ? offset+tmp-buf : 0);
      out.variables_count++;
    }
    tmp += len;
  }
  while (tmp != buf+size) {
    if (tmp-buf >= UINT32_MAX || !check_sum(tmp-buf, 4, size)) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || !check_sum(tmp-buf, len, size)) error("Invalid size");
    parse_class_property(tmp, len, &out);
    tmp += len;
  }
  return out;
}

static struct mof_class parse_class(char *buf, uint32_t size, uint32_t offset) {
  struct mof_class out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 8) error("Invalid size");
  if (buf2[1] != 0x0) error("Invalid unknown");
  if (size < 20) {
    fprintf(stderr, "Warning: no class defined\n");
    return out;
  }
  uint32_t len1 = buf2[2];
  uint32_t len = buf2[3];
  if (!check_sum(20, len, size)) error("Invalid size");
  if (len1 > len) error("Invalid size");
  if (buf2[4] == 0x1) {
    fprintf(stderr, "Warning: Instance of class is not supported yet\n");
    return out;
  } else if (buf2[4] != 0x0) {
    fprintf(stderr, "Warning: Class has unknown value 0x%x\n", buf2[4]);
    return out;
  }
  out = parse_class_data(buf+20, len, len1, 1, offset ? offset+20 : 0);
  buf += 20 + len;
  size -= 20 + len;
  if (offset)
    offset += 20 + len;
  if (size < 4) error("Invalid size");
  buf2 = (uint32_t *)buf;
  len = buf2[0];
  if (len < 8 || len > size) error("Invalid size");
  uint32_t count = buf2[1];
  uint32_t i;
  buf += 8;
  size -= 8;
  if (offset)
    offset += 8;
  out.methods_count = count;
  out.methods = calloc(count, sizeof(*out.methods));
  if (!out.methods) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (size < 4) error("Invalid size");
    uint32_t len1 = ((uint32_t *)buf)[0];
    if (len1 == 0 || len1 > size) error("Invalid size");
    out.methods[i] = parse_class_method(buf, len1, offset);
    buf += len1;
    size -= len1;
    if (offset)
      offset += len1;
  }
  return out;
}

static void free_qualifier(struct mof_qualifier *qualifier) {
  if (!qualifier)
    return;
  free(qualifier->name);
  if (qualifier->type == MOF_QUALIFIER_STRING)
    free(qualifier->value.string);
}

static void free_qualifiers(struct mof_qualifier *qualifiers, uint32_t count) {
  uint32_t i;
  for (i=0; i<count; ++i)
    free_qualifier(&qualifiers[i]);
  free(qualifiers);
}

static void free_variable(struct mof_variable *variable) {
  if (!variable)
    return;
  free(variable->name);
  free_qualifiers(variable->qualifiers, variable->qualifiers_count);
  if (variable->variable_type == MOF_VARIABLE_OBJECT || variable->variable_type == MOF_VARIABLE_OBJECT_ARRAY)
    free(variable->type.object);
}

static void free_variables(struct mof_variable *variables, uint32_t count) {
  uint32_t i;
  for (i=0; i<count; ++i)
    free_variable(&variables[i]);
  free(variables);
}

static void free_method(struct mof_method *method) {
  if (!method)
    return;
  free_qualifiers(method->qualifiers, method->qualifiers_count);
  free(method->name);
  free_variables(method->parameters, method->parameters_count);
  free_variable(&method->return_value);
  free(method->parameters_direction);
}

static void free_methods(struct mof_method *methods, uint32_t count) {
  uint32_t i;
  for (i=0; i<count; ++i)
    free_method(&methods[i]);
  free(methods);
}

static void free_class(struct mof_class *class) {
  if (!class)
    return;
  free(class->name);
  free(class->namespace);
  free(class->superclassname);
  free_qualifiers(class->qualifiers, class->qualifiers_count);
  free_variables(class->variables, class->variables_count);
  free_methods(class->methods, class->methods_count);
}

static void free_classes(struct mof_class *classes, uint32_t count) {
  uint32_t i;
  for (i=0; i<count; ++i)
    free_class(&classes[i]);
  free(classes);
}

static struct mof_classes parse_root(char *buf, uint32_t size, uint32_t offset) {
  struct mof_classes out;
  memset(&out, 0, sizeof(out));
  if (size < 12) error("Invalid size");
  uint32_t *buf2 = (uint32_t *)buf;
  if (buf2[0] != 0x1 || buf2[1] != 0x1) error("Invalid unknown");
  uint32_t count = buf2[2];
  uint32_t i;
  char *tmp = buf + 12;
  out.count = count;
  out.classes = calloc(count, sizeof(*out.classes));
  if (!out.classes) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (tmp-buf >= UINT32_MAX || !check_sum(tmp-buf, 4, size)) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || !check_sum(tmp-buf, len, size)) error("Invalid size");
    out.classes[i] = parse_class(tmp, len, offset ? offset+tmp-buf : 0);
    tmp += len;
  }
  if (tmp != buf+size) error("Buffer not processed");
  return out;
}

static struct mof_classes parse_bmf(char *buf, uint32_t size) {
  struct mof_classes out;
  if (size < 8) error("Invalid file size");
  if (((uint32_t *)buf)[0] != 0x424D4F46) error("Invalid magic header");
  uint32_t len = ((uint32_t *)buf)[1];
  if (len > size) error("Invalid size");
  uint32_t i;
  uint32_t count = 0;
  if (len < size) {
    if (!check_sum(20, len, size)) error("Invalid size");
    if (memcmp(buf+len, "BMOFQUALFLAVOR11", 16) != 0) error("Invalid second magic header");
    count = ((uint32_t *)(buf+len+16))[0];
    if (count >= UINT32_MAX/8 || 8*count != size-len-16-4) error("Invalid size");
    for (i=0; i<count; ++i) {
      if (((uint32_t *)(buf+len+16+4))[2*i] == 0) error("Invalid offset in second part");
    }
  }
  out = parse_root(buf+8, len-8, (len < size) ? 8 : 0);
  for (i=0; i<count; ++i) {
    if (((uint32_t *)(buf+len+16+4))[2*i] != 0) error("Qualifier from second part was not parsed");
  }
  return out;
}

static void print_qualifiers(struct mof_qualifier *qualifiers, uint32_t count, int indent) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    printf("%*.sQualifier %u:\n", indent, "", i);
    printf("%*.s  Name=%s\n", indent, "", qualifiers[i].name);
    printf("%*.s  Tosubclass=%s\n", indent, "", qualifiers[i].tosubclass ? "TRUE" : "FALSE");
    switch (qualifiers[i].type) {
    case MOF_QUALIFIER_BOOLEAN:
      printf("%*.s  Type=Boolean\n", indent, "");
      printf("%*.s  Value=%s\n", indent, "", qualifiers[i].value.boolean ? "TRUE" : "FALSE");
      break;
    case MOF_QUALIFIER_SINT32:
      printf("%*.s  Type=Numeric\n", indent, "");
      printf("%*.s  Value=%d\n", indent, "", qualifiers[i].value.sint32);
      break;
    case MOF_QUALIFIER_STRING:
      printf("%*.s  Type=String\n", indent, "");
      printf("%*.s  Value=%s\n", indent, "", qualifiers[i].value.string);
      break;
    default:
      printf("%*.s  Type=Unknown\n", indent, "");
      break;
    }
  }
}

static void print_variable_type(struct mof_variable *variable, int with_info) {
  char *variable_type = "unknown";
  char *type = NULL;
  switch (variable->variable_type) {
  case MOF_VARIABLE_BASIC:
  case MOF_VARIABLE_BASIC_ARRAY:
    variable_type = "Basic";
    switch (variable->type.basic) {
    case MOF_BASIC_TYPE_STRING: type = "String"; break;
    case MOF_BASIC_TYPE_SINT32: type = "sint32"; break;
    case MOF_BASIC_TYPE_UINT32: type = "uint32"; break;
    case MOF_BASIC_TYPE_SINT16: type = "sint16"; break;
    case MOF_BASIC_TYPE_UINT16: type = "uint16"; break;
    case MOF_BASIC_TYPE_SINT64: type = "sint64"; break;
    case MOF_BASIC_TYPE_UINT64: type = "uint64"; break;
    case MOF_BASIC_TYPE_SINT8: type = "sint8"; break;
    case MOF_BASIC_TYPE_UINT8: type = "uint8"; break;
    case MOF_BASIC_TYPE_DATETIME: type = "Datetime"; break;
    case MOF_BASIC_TYPE_BOOLEAN: type = "Boolean"; break;
    default: type = "unknown"; break;
    }
    break;
  case MOF_VARIABLE_OBJECT:
  case MOF_VARIABLE_OBJECT_ARRAY:
    variable_type = "Object";
    type = variable->type.object;
    break;
  default:
    break;
  }
  if (with_info) {
    printf("%s", variable_type);
    if (type)
      printf(":%s", type);
    if (variable->variable_type == MOF_VARIABLE_BASIC_ARRAY || variable->variable_type == MOF_VARIABLE_OBJECT_ARRAY)
      printf("[%d]", variable->array);
  } else {
    printf("%s", type ? type : "unknown");
  }
}

static void print_variable(struct mof_variable *variable, int indent) {
  printf("%*.s  Name=%s\n", indent, "", variable->name);
  printf("%*.s  Type=", indent, "");
  print_variable_type(variable, 1);
  printf("\n");
  print_qualifiers(variable->qualifiers, variable->qualifiers_count, indent+2);
}

static void print_variables(struct mof_variable *variables, uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    printf("  Variable %u:\n", i);
    print_variable(&variables[i], 2);
  }
}

static void print_parameters(struct mof_method *method) {
  uint32_t i;
  for (i = 0; i < method->parameters_count; ++i) {
    printf("    Parameter %u:\n", i);
    printf("      Direction=");
    switch (method->parameters_direction[i]) {
    case MOF_PARAMETER_IN:
      printf("in");
      break;
    case MOF_PARAMETER_OUT:
      printf("out");
      break;
    case MOF_PARAMETER_IN_OUT:
      printf("in+out");
      break;
    default:
      printf("unknown");
      break;
    }
    printf("\n");
    print_variable(&method->parameters[i], 4);
  }
}

static void print_classes(struct mof_class *classes, uint32_t count) {
  uint32_t i, j;
  for (i = 0; i < count; ++i) {
    printf("Class %u:\n", i);
    printf("  Name=%s\n", classes[i].name);
    printf("  Superclassname=%s\n", classes[i].superclassname);
    printf("  Classflags=%d\n", (int)classes[i].classflags);
    printf("  Namespace=%s\n", classes[i].namespace);
    print_qualifiers(classes[i].qualifiers, classes[i].qualifiers_count, 2);
    print_variables(classes[i].variables, classes[i].variables_count);
    for (j = 0; j < classes[i].methods_count; ++j) {
      printf("  Method %u:\n", j);
      printf("    Name=%s\n", classes[i].methods[j].name);
      print_qualifiers(classes[i].methods[j].qualifiers, classes[i].methods[j].qualifiers_count, 4);
      printf("    Return value:\n");
      printf("      Type=");
      if (classes[i].methods[j].return_value.variable_type)
         print_variable_type(&classes[i].methods[j].return_value, 1);
      else
         printf("Void");
      printf("\n");
      print_parameters(&classes[i].methods[j]);
    }
  }
}

#undef print_classes
static void print_classes(struct mof_class *classes, uint32_t count);

static int process_data(char *data, uint32_t size) {
  struct mof_classes classes;
  classes = parse_bmf(data, size);
  print_classes(classes.classes, classes.count);
  free_classes(classes.classes, classes.count);
  return 0;
}
