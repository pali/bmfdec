/*
    bmf2mof.c - Decompile binary MOF file (BMF) to UTF-8 plain text MOF file
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

#define print_classes bmfparse_print_classes
#define print_variable bmfparse_print_variable
#define print_qualifiers bmfparse_print_qualifiers
#define print_variable_type bmfparse_print_variable_type
#include "bmfparse.c"
#undef print_classes
#undef print_variable
#undef print_qualifiers
#undef print_variable_type

static void print_string(char *str) {
  int len = strlen(str);
  int i;
  for (i=0; i<len; ++i) {
    if (str[i] == '"' || str[i] == '\\')
      putchar('\\');
    putchar(str[i]);
  }
}

static void print_qualifiers(struct mof_qualifier *qualifiers, uint32_t count, char *prefix) {
  uint32_t i;
  if (count > 0 || prefix) {
    printf("[");
    if (prefix) {
      printf("%s", prefix);
      if (count > 0)
        printf(", ");
    }
    for (i = 0; i < count; ++i) {
      switch (qualifiers[i].type) {
      case MOF_QUALIFIER_BOOLEAN:
        print_string(qualifiers[i].name);
        if (!qualifiers[i].value.boolean)
          printf("(FALSE)");
        break;
      case MOF_QUALIFIER_SINT32:
        print_string(qualifiers[i].name);
        printf("(%d)", qualifiers[i].value.sint32);
        break;
      case MOF_QUALIFIER_STRING:
        print_string(qualifiers[i].name);
        printf("(\"");
        print_string(qualifiers[i].value.string);
        printf("\")");
        break;
      default:
        printf("unknown");
        break;
      }
      if (qualifiers[i].toinstance || qualifiers[i].tosubclass || qualifiers[i].disableoverride || qualifiers[i].amended) {
        printf(" :");
        if (qualifiers[i].toinstance)
          printf(" ToInstance");
        if (qualifiers[i].tosubclass)
          printf(" ToSubclass");
        if (qualifiers[i].disableoverride)
          printf(" DisableOverride");
        if (qualifiers[i].amended)
          printf(" Amended");
      }
      if (i != count-1)
        printf(", ");
    }
    printf("]");
  }
}

static void print_variable_type(struct mof_variable *variable) {
  char *type = NULL;
  switch (variable->variable_type) {
  case MOF_VARIABLE_BASIC:
  case MOF_VARIABLE_BASIC_ARRAY:
    switch (variable->type.basic) {
    case MOF_BASIC_TYPE_STRING: type = "string"; break;
    case MOF_BASIC_TYPE_REAL64: type = "real64"; break;
    case MOF_BASIC_TYPE_REAL32: type = "real32"; break;
    case MOF_BASIC_TYPE_SINT32: type = "sint32"; break;
    case MOF_BASIC_TYPE_UINT32: type = "uint32"; break;
    case MOF_BASIC_TYPE_SINT16: type = "sint16"; break;
    case MOF_BASIC_TYPE_UINT16: type = "uint16"; break;
    case MOF_BASIC_TYPE_SINT64: type = "sint64"; break;
    case MOF_BASIC_TYPE_UINT64: type = "uint64"; break;
    case MOF_BASIC_TYPE_SINT8: type = "sint8"; break;
    case MOF_BASIC_TYPE_UINT8: type = "uint8"; break;
    case MOF_BASIC_TYPE_DATETIME: type = "datetime"; break;
    case MOF_BASIC_TYPE_CHAR16: type = "char16"; break;
    case MOF_BASIC_TYPE_BOOLEAN: type = "boolean"; break;
    default: break;
    }
    break;
  case MOF_VARIABLE_OBJECT:
  case MOF_VARIABLE_OBJECT_ARRAY:
    type = variable->type.object;
    break;
  default:
    break;
  }
  printf("%s", type ? type : "unknown");
}

static void print_variable(struct mof_variable *variable, char *prefix) {
  if (variable->qualifiers_count > 0 || prefix) {
    print_qualifiers(variable->qualifiers, variable->qualifiers_count, prefix);
    printf(" ");
  }
  print_variable_type(variable);
  printf(" ");
  print_string(variable->name);
  if (variable->variable_type == MOF_VARIABLE_BASIC_ARRAY || variable->variable_type == MOF_VARIABLE_OBJECT_ARRAY) {
    printf("[");
    if (variable->has_array_max)
      printf("%d", variable->array_max);
    printf("]");
  }
}

static void print_classes(struct mof_class *classes, uint32_t count) {
  char *direction;
  uint32_t i, j, k;
  int print_namespace = 0;
  int print_classflags = 0;
  for (i = 0; i < count; ++i) {
    if (!classes[i].name)
      continue;
    if (classes[i].namespace && strcmp(classes[i].namespace, "root\\default") != 0)
      print_namespace = 1;
    if (classes[i].classflags)
      print_classflags = 1;
  }
  for (i = 0; i < count; ++i) {
    if (!classes[i].name)
      continue;
    if (print_namespace) {
      printf("#pragma namespace(\"");
      if (classes[i].namespace)
        print_string(classes[i].namespace);
      else
        print_string("root\\default");
      printf("\")\n");
    }
    if (print_classflags) {
      printf("#pragma classflags(");
      if (classes[i].classflags == 1)
        printf("\"updateonly\"");
      else if (classes[i].classflags == 2)
        printf("\"createonly\"");
      else if (classes[i].classflags == 32)
        printf("\"safeupdate\"");
      else if (classes[i].classflags == 33)
        printf("\"updateonly\", \"safeupdate\"");
      else if (classes[i].classflags == 64)
        printf("\"forceupdate\"");
      else if (classes[i].classflags == 65)
        printf("\"updateonly\", \"forceupdate\"");
      else
        printf("%d", (int)classes[i].classflags);
      printf(")\n");
    }
    if (classes[i].qualifiers_count > 0) {
      print_qualifiers(classes[i].qualifiers, classes[i].qualifiers_count, NULL);
      printf("\n");
    }
    printf("class ");
    print_string(classes[i].name);
    printf(" ");
    if (classes[i].superclassname) {
      printf(": ");
      print_string(classes[i].superclassname);
      printf(" ");
    }
    printf("{\n");
    for (j = 0; j < classes[i].variables_count; ++j) {
      printf("  ");
      print_variable(&classes[i].variables[j], NULL);
      printf(";\n");
    }
    if (classes[i].variables_count && classes[i].methods_count)
      printf("\n");
    for (j = 0; j < classes[i].methods_count; ++j) {
      printf("  ");
      if (classes[i].methods[j].qualifiers_count > 0) {
        print_qualifiers(classes[i].methods[j].qualifiers, classes[i].methods[j].qualifiers_count, NULL);
        printf(" ");
      }
      if (classes[i].methods[j].return_value.variable_type)
        print_variable_type(&classes[i].methods[j].return_value);
      else
        printf("void");
      printf(" ");
      print_string(classes[i].methods[j].name);
      printf("(");
      for (k = 0; k < classes[i].methods[j].parameters_count; ++k) {
        switch (classes[i].methods[j].parameters_direction[k]) {
        case MOF_PARAMETER_IN:
          direction = "in";
          break;
        case MOF_PARAMETER_OUT:
          direction = "out";
          break;
        case MOF_PARAMETER_IN_OUT:
          direction = "in, out";
          break;
        default:
          direction = NULL;
          break;
        }
        print_variable(&classes[i].methods[j].parameters[k], direction);
        if (k != classes[i].methods[j].parameters_count-1)
          printf(", ");
      }
      printf(");\n");
    }
    printf("};\n");
    if (i != count-1)
      printf("\n");
  }
}
