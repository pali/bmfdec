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
#include "bmfparse.c"
#undef print_classes
#undef print_variable
#undef print_qualifiers

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
      if (qualifiers[i].tosubclass)
        printf(" : ToSubclass");
      if (i != count-1)
        printf(", ");
    }
    printf("]");
  }
}

static void print_variable(struct mof_variable *variable, char *prefix) {
  if (variable->qualifiers_count > 0 || prefix) {
    print_qualifiers(variable->qualifiers, variable->qualifiers_count, prefix);
    printf(" ");
  }
  print_variable_type(variable, 0);
  printf(" ");
  print_string(variable->name);
  if (variable->variable_type == MOF_VARIABLE_BASIC_ARRAY || variable->variable_type == MOF_VARIABLE_OBJECT_ARRAY)
    printf("[%d]", variable->array);
}

static void print_classes(struct mof_class *classes, uint32_t count) {
  char *direction;
  uint32_t i, j, k;
  int print_namespace = 0;
  for (i = 0; i < count; ++i) {
    if (!classes[i].name)
      continue;
    if (classes[i].namespace && (print_namespace || strcmp(classes[i].namespace, "root\\default") != 0)) {
      printf("#pragma namespace(\"");
      print_string(classes[i].namespace);
      printf("\")\n");
      print_namespace = 1;
    }
    if (classes[i].classflags)
      printf("#pragma classflags(%d)\n", (int)classes[i].classflags);
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
        print_variable_type(&classes[i].methods[j].return_value, 0);
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
