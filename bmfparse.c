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

#define main bmfdec_main
#include "bmfdec.c"
#undef main

#include <stdlib.h>
#include <string.h>

#define error(str) do { fprintf(stderr, "error %s at %s:%d\n", str, __func__, __LINE__); exit(1); } while (0)

enum mof_qualifier_type {
  MOF_QUALIFIER_UNKNOWN,
  MOF_QUALIFIER_VOID,
  MOF_QUALIFIER_NUMERIC,
  MOF_QUALIFIER_STRING,
};

enum mof_variable_type {
  MOF_VARIABLE_UNKNOWN,
  MOF_VARIABLE_BASIC,
  MOF_VARIABLE_OBJECT,
  MOF_VARIABLE_ARRAY,
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

struct mof_qualifier {
  enum mof_qualifier_type type;
  char *name;
  union {
    uint32_t numeric;
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
  uint32_t array;
};

struct mof_method {
  uint32_t qualifiers_count;
  struct mof_qualifier *qualifiers;
  char *name;
  uint32_t inputs_count;
  struct mof_variable *inputs;
  uint32_t outputs_count;
  struct mof_variable *outputs;
  struct mof_variable return_value;
};

struct mof_class {
  char *name;
  char *namespace;
  char *superclassname;
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
  char *out = malloc((size/2)*3+1);
  if (!out) error("malloc failed");
  uint32_t i, j;
  for (i=0, j=0; i<size/2; ++i) {
    if (buf2[i] == 0) {
      out[j++] = 0;
      break;
    } else if (buf2[i] < 0x80) {
      out[j++] = buf2[i];
    } else if (buf2[i] < 0x800) {
      out[j++] = 0xC0 | (buf2[i] >> 6);
      out[j++] = 0x80 | (buf2[i] & 0x3F);
    } else {
      out[j++] = 0xE0 | (buf2[i] >> 12);
      out[j++] = 0x80 | ((buf2[i] >> 6) & 0x3F);
      out[j++] = 0x80 | (buf2[i] & 0x3F);
    }
  }
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

static struct mof_qualifier parse_qualifier_void(char *buf, uint32_t size, uint32_t val) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  if (val != 0xFFFF) error("Invalid unknown");
  out.type = MOF_QUALIFIER_VOID;
  out.name = parse_string(buf, size);
  return out;
}

static struct mof_qualifier parse_qualifier_numeric(char *buf, uint32_t size, uint32_t val) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  out.type = MOF_QUALIFIER_NUMERIC;
  out.name = parse_string(buf, size);
  out.value.numeric = val;
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

static struct mof_qualifier parse_qualifier(char *buf, uint32_t size) {
  struct mof_qualifier out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 16) error("Invalid size");
  uint32_t type = buf2[1];
  uint32_t len = buf2[3];
  if (len+16 > size) error("Invalid size");
  uint32_t val = 0xFFFF;
  if (len+16+4 > size)
    fprintf(stderr, "Warning: no qualifier value, using 0xFFFF\n");
  else
    val = *((uint32_t *)(buf+16+len));
  switch (type) {
  case 0x0B:
    out = parse_qualifier_void(buf+16, len, val);
    if (16+len+4 < size) error("Buffer not processed");
    break;
  case 0x03:
    out = parse_qualifier_numeric(buf+16, len, val);
    if (16+len+4 < size) error("Buffer not processed");
    break;
  case 0x08:
    out = parse_qualifier_string(buf+16, len, buf+16+len, size-len-16);
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
  return out;
}

static struct mof_variable parse_class_variable(char *buf, uint32_t size) {
  struct mof_variable out;
  memset(&out, 0, sizeof(out));
  uint32_t *buf2 = (uint32_t *)buf;
  if (size < 20) error("Invalid size");
  uint32_t type = buf2[1];
  switch (type) {
  case 0x08: // string
  case 0x0B: // boolean
  case 0x13:
    out.variable_type = MOF_VARIABLE_BASIC;
    break;
  case 0x0D:
    out.variable_type = MOF_VARIABLE_OBJECT;
    break;
  case 0x2011:
    out.variable_type = MOF_VARIABLE_ARRAY;
    break;
  default:
    fprintf(stderr, "Warning: unknown variable type 0x%x\n", type);
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf, size);
    return out;
  }
  if (buf2[2] != 0x0 || buf2[3] != 0xFFFFFFFF) error("Invalid unknown");
  uint32_t len = buf2[4];
  if (20+len > size) error("Invalid size");
  out.name = parse_string(buf+20, len);
  if (20+len+8 > size) error("Invalid size");
  buf2 = (uint32_t *)(buf+20+len);
  uint32_t len1 = buf2[0];
  if (20+len+len1 > size) error("Invalid size");
  uint32_t count = buf2[1];
  uint32_t i;
  char *tmp = buf+20+len+8;
  out.qualifiers = calloc(count, sizeof(*out.qualifiers));
  if (!out.qualifiers) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (tmp+4 > buf+20+len+8+len1) error("Invalid size");
    uint32_t len2 = ((uint32_t *)tmp)[0];
    if (len2 == 0 || tmp+len2 > buf+20+len+8+len1) error("Invalid size");
    out.qualifiers[out.qualifiers_count] = parse_qualifier(tmp, len2);
    if (out.qualifiers[out.qualifiers_count].name) {
      if (out.qualifiers[out.qualifiers_count].type == MOF_QUALIFIER_STRING && strcmp(out.qualifiers[out.qualifiers_count].name, "CIMTYPE") == 0) {
        if (out.variable_type == MOF_VARIABLE_OBJECT) {
          if (strncmp(out.qualifiers[out.qualifiers_count].value.string, "object:", strlen("object:")) == 0) {
            out.type.object = out.qualifiers[out.qualifiers_count].value.string + strlen("object:");
            free(out.qualifiers[out.qualifiers_count].name);
          } else {
            out.qualifiers_count++;
            error("object without 'object:' in CIMTYPE");
          }
        } else {
          char *strtype = out.qualifiers[out.qualifiers_count].value.string;
          if (strcmp(strtype, "String") == 0 || strcmp(strtype, "string") == 0)
            out.type.basic = MOF_BASIC_TYPE_STRING;
          else if (strcmp(strtype, "sint32") == 0)
            out.type.basic = MOF_BASIC_TYPE_SINT32;
          else if (strcmp(strtype, "uint32") == 0)
            out.type.basic = MOF_BASIC_TYPE_UINT32;
          else if (strcmp(strtype, "sint16") == 0)
            out.type.basic = MOF_BASIC_TYPE_SINT16;
          else if (strcmp(strtype, "uint16") == 0)
            out.type.basic = MOF_BASIC_TYPE_UINT16;
          else if (strcmp(strtype, "sint64") == 0)
            out.type.basic = MOF_BASIC_TYPE_SINT64;
          else if (strcmp(strtype, "uint64") == 0)
            out.type.basic = MOF_BASIC_TYPE_UINT64;
          else if (strcmp(strtype, "sint8") == 0)
            out.type.basic = MOF_BASIC_TYPE_SINT8;
          else if (strcmp(strtype, "uint8") == 0)
            out.type.basic = MOF_BASIC_TYPE_UINT8;
          else if (strcmp(strtype, "Datetime") == 0 || strcmp(strtype, "datetime") == 0)
            out.type.basic = MOF_BASIC_TYPE_DATETIME;
          else if (strcmp(strtype, "Boolean") == 0 || strcmp(strtype, "boolean") == 0)
            out.type.basic = MOF_BASIC_TYPE_BOOLEAN;
          else
            error("unknown basic type");
          free(out.qualifiers[out.qualifiers_count].value.string);
          free(out.qualifiers[out.qualifiers_count].name);
        }
      } else if (out.qualifiers[out.qualifiers_count].type == MOF_QUALIFIER_NUMERIC && strcmp(out.qualifiers[out.qualifiers_count].name, "MAX") == 0 && out.variable_type == MOF_VARIABLE_ARRAY) {
        out.array = out.qualifiers[out.qualifiers_count].value.numeric;
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

static struct mof_method parse_class_method(char *buf, uint32_t size) {
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
    // TODO: Parse method arguments
    fprintf(stderr, "Warning: method arguments not parsed yet\n");
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf+20+len, buf2[4]);
  }
  if (20+len > size) error("Invalid size");
  out.name = parse_string(buf+20, len);
  len = buf2[4];
  buf2 = (uint32_t *)(buf+20+len);
  uint32_t len1 = buf2[0];
  if (20+len+len1 > size) error("Invalid size");
  uint32_t count = buf2[1];
  uint32_t i;
  char *tmp = buf+20+len+8;
  out.qualifiers_count = count;
  out.qualifiers = calloc(count, sizeof(*out.qualifiers));
  if (!out.qualifiers) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (tmp+4 > buf+20+len+8+len1) error("Invalid size");
    uint32_t len2 = ((uint32_t *)tmp)[0];
    if (len2 == 0 || tmp+len2 > buf+20+len+8+len1) error("Invalid size");
    out.qualifiers[i] = parse_qualifier(tmp, len2);
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
  if (buf2[1] != 0x08 || buf2[2] != 0x0) error("Invalid unknown");
  if (buf2[4] != 0xFFFFFFFF) {
    fprintf(stderr, "Warning: cannot parse unknown 0x%x in class property\n", buf2[4]);
    fprintf(stderr, "Hexdump:\n");
    dump_bytes(buf, size);
    return;
  }
  uint32_t slen = buf2[3];
  if (size < slen+20) error("Invalid size");
  char *name = parse_string(buf+20, slen);
  char *value = parse_string(buf+20+slen, size-slen-20);
  if (strcmp(name, "__CLASS") == 0) {
    out->name = value;
  } else if (strcmp(name, "__NAMESPACE") == 0) {
    out->namespace = value;
  } else if (strcmp(name, "__SUPERCLASS") == 0) {
    out->superclassname = value;
  } else {
    fprintf(stderr, "Warning: Unknown class property name %s\n", name);
  }
}

static struct mof_class parse_class_data(char *buf, uint32_t size, uint32_t size1) {
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
  out.qualifiers_count = count1;
  out.qualifiers = calloc(count1, sizeof(*out.qualifiers));
  if (!out.qualifiers) error("calloc failed");
  for (i=0; i<count1; ++i) {
    if (tmp+4 > buf+len1) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || tmp+len > buf+len1) error("Invalid size");
    out.qualifiers[i] = parse_qualifier(tmp, len);
    tmp += len;
  }
  buf2 = (uint32_t *)tmp;
  uint32_t len2 = buf2[0];
  uint32_t count2 = buf2[1];
  if (len1+len2 > size) error("Invalid size");
  tmp += 8;
  out.variables = calloc(count2, sizeof(*out.variables));
  if (!out.variables) error("calloc failed");
  for (i=0; i<count2; ++i) {
    if (tmp+4 > buf+len1+len2) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || tmp+len > buf+len1+len2) error("Invalid size");
    if (tmp+16 <= buf+len1+len2 && ((uint32_t *)tmp)[4] == 0xFFFFFFFF) {
      parse_class_property(tmp, len, &out);
    } else {
      out.variables[i] = parse_class_variable(tmp, len);
      out.variables_count++;
    }
    tmp += len;
  }
  while (tmp != buf+size) {
    if (tmp+4 > buf+size) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || tmp+len > buf+size) error("Invalid size");
    parse_class_property(tmp, len, &out);
    tmp += len;
  }
  return out;
}

static struct mof_class parse_class(char *buf, uint32_t size) {
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
  if (buf2[4] != 0x0) error("Invalid unknown");
  if (len + 20 > size) error("Invalid size");
  if (len1 > len) error("Invalid size");
  out = parse_class_data(buf+20, len, len1);
  buf += 20 + len;
  size -= 20 + len;
  if (size < 4) error("Invalid size");
  buf2 = (uint32_t *)buf;
  len = buf2[0];
  if (len < 8 || len > size) error("Invalid size");
  uint32_t count = buf2[1];
  uint32_t i;
  buf += 8;
  size -= 8;
  out.methods_count = count;
  out.methods = calloc(count, sizeof(*out.methods));
  if (!out.methods) error("calloc failed");
  for (i=0; i<count; ++i) {
    if (size < 4) error("Invalid size");
    uint32_t len1 = ((uint32_t *)buf)[0];
    if (len1 == 0 || len1 > size) error("Invalid size");
    out.methods[i] = parse_class_method(buf, len1);
    buf += len1;
    size -= len1;
  }
  return out;
}

static struct mof_classes parse_root(char *buf, uint32_t size) {
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
    if (tmp+4 > buf+size) error("Invalid size");
    uint32_t len = ((uint32_t *)tmp)[0];
    if (len == 0 || tmp+len > buf+size) error("Invalid size");
    out.classes[i] = parse_class(tmp, len);
    tmp += len;
  }
  if (tmp != buf+size) error("Buffer not processed");
  return out;
}

static struct mof_classes parse_bmf(char *buf, uint32_t size) {
  if (size < 8) error("Invalid file size");
  if (((uint32_t *)buf)[0] != 0x424D4F46) error("Invalid magic header");
  uint32_t len = ((uint32_t *)buf)[1];
  if (len > size) error("Invalid size");
  return parse_root(buf+8, len-8);
}

static void print_qualifiers(struct mof_qualifier *qualifiers, uint32_t count, int indent) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    printf("%*.sQualifier %d:\n", indent, "", i);
    printf("%*.s  Name=%s\n", indent, "", qualifiers[i].name);
    switch (qualifiers[i].type) {
    case MOF_QUALIFIER_VOID:
      printf("%*.s  Type=Void\n", indent, "");
      break;
    case MOF_QUALIFIER_NUMERIC:
      printf("%*.s  Type=Numeric\n", indent, "");
      printf("%*.s  Value=%u\n", indent, "", qualifiers[i].value.numeric);
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

static char *get_variable_type(struct mof_variable *variable) {
  char *type = "unknown";
  switch (variable->variable_type) {
  case MOF_VARIABLE_BASIC:
  case MOF_VARIABLE_ARRAY:
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
    type = variable->type.object;
    break;
  default:
    type = "unknown";
    break;
  }
  return type;
}

static void print_variables(struct mof_variable *variables, uint32_t count, int indent, char *name) {
  uint32_t i;
  char *type;
  for (i = 0; i < count; ++i) {
    printf("%*.s%s %d:\n", indent, "", name, i);
    printf("%*.s  Name=%s\n", indent, "", variables[i].name);
    type = get_variable_type(&variables[i]);
    if (variables[i].variable_type == MOF_VARIABLE_ARRAY)
      printf("%*.s  Type=%s[%d]\n", indent, "", type, variables[i].array);
    else
      printf("%*.s  Type=%s\n", indent, "", type);
    print_qualifiers(variables[i].qualifiers, variables[i].qualifiers_count, 4);
  }
}

static void print_classes(struct mof_class *classes, uint32_t count) {
  uint32_t i, j;
  for (i = 0; i < count; ++i) {
    printf("Class %d:\n", i);
    printf("  Name=%s\n", classes[i].name);
    printf("  Superclassname=%s\n", classes[i].superclassname);
    printf("  Namespace=%s\n", classes[i].namespace);
    print_qualifiers(classes[i].qualifiers, classes[i].qualifiers_count, 2);
    print_variables(classes[i].variables, classes[i].variables_count, 2, "Variable");
    for (j = 0; j < classes[i].methods_count; ++j) {
      printf("  Method %d:\n", j);
      printf("    Name=%s\n", classes[i].methods[j].name);
      print_qualifiers(classes[i].methods[j].qualifiers, classes[i].methods[j].qualifiers_count, 4);
      print_variables(classes[i].methods[j].inputs, classes[i].methods[j].inputs_count, 4, "Input parameter");
      print_variables(classes[i].methods[j].outputs, classes[i].methods[j].outputs_count, 4, "Output parameter");
      printf("    Return value:\n");
      printf("      Type=%s\n", classes[i].methods[j].return_value.variable_type ? get_variable_type(&classes[i].methods[j].return_value) : "void");
    }
  }
}

int main() {
  char pin[0x10000];
  char pout[0x10000];
  int lin, lout;
  struct mof_classes classes;
  lin = read(0, pin, sizeof(pin));
  if (lin <= 16 || lin == sizeof(pin) || ((uint32_t *)pin)[0] != 0x424D4F46 || ((uint32_t *)pin)[1] != 0x01 || ((uint32_t *)pin)[2] != (uint32_t)lin-16 || ((uint32_t *)pin)[3] > sizeof(pout)) {
    fprintf(stderr, "Invalid input\n");
    return 1;
  }
  lout = ((uint32_t *)pin)[3];
  if (ds_dec(pin+16, lin-16, pout, lout, 0) != lout) {
    fprintf(stderr, "Decompress failed\n");
    return 1;
  }
  classes = parse_bmf(pout, lout);
  print_classes(classes.classes, classes.count);
  return 0;
}
