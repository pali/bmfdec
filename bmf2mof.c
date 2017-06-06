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
#define print_qualifiers bmfparse_print_qualifiers
#include "bmfparse.c"
#undef print_classes
#undef print_qualifiers

static void print_qualifiers(struct mof_qualifier *qualifiers, uint32_t count, int indent) {
  uint32_t i;
  if (count > 0) {
    printf("[");
    for (i = 0; i < count; ++i) {
      switch (qualifiers[i].type) {
      case MOF_QUALIFIER_BOOLEAN:
        printf("%s", qualifiers[i].name);
        if (!qualifiers[i].value.boolean)
          printf("(FALSE)");
        break;
      case MOF_QUALIFIER_SINT32:
        printf("%s(%d)", qualifiers[i].name, qualifiers[i].value.sint32);
        break;
      case MOF_QUALIFIER_STRING:
        printf("%s(\"%s\")", qualifiers[i].name, qualifiers[i].value.string);
        break;
      default:
        printf("unknown");
        break;
      }
      if (i != count-1)
        printf(", ");
    }
    printf("]");
  }
}

static void print_variable(struct mof_variable *variable) {
  if (variable->qualifiers_count > 0) {
    print_qualifiers(variable->qualifiers, variable->qualifiers_count, 0);
    printf(" ");
  }
  print_variable_type(variable, 0);
  printf(" %s", variable->name);
  if (variable->variable_type == MOF_VARIABLE_BASIC_ARRAY || variable->variable_type == MOF_VARIABLE_OBJECT_ARRAY)
    printf("[%d]", variable->array);
}

static void print_classes(struct mof_class *classes, uint32_t count) {
  uint32_t i, j, k;
  for (i = 0; i < count; ++i) {
    // TODO: namespace
    if (classes[i].qualifiers_count > 0) {
      print_qualifiers(classes[i].qualifiers, classes[i].qualifiers_count, 0);
      printf("\n");
    }
    printf("class %s ", classes[i].name);
    if (classes[i].superclassname)
      printf(": %s ", classes[i].superclassname);
    printf("{\n");
    for (j = 0; j < classes[i].variables_count; ++j) {
      printf("  ");
      print_variable(&classes[i].variables[j]);
      printf(";\n");
    }
    if (classes[i].variables_count && classes[i].methods_count)
      printf("\n");
    for (j = 0; j < classes[i].methods_count; ++j) {
      printf("  ");
      if (classes[i].methods[j].qualifiers_count > 0) {
        print_qualifiers(classes[i].methods[j].qualifiers, classes[i].methods[j].qualifiers_count, 0);
        printf(" ");
      }
      if (classes[i].methods[j].return_value.variable_type)
        print_variable_type(&classes[i].methods[j].return_value, 0);
      else
        printf("void");
      printf(" %s(", classes[i].methods[j].name);
      // TODO: fix order of parameters
      for (k = 0; k < classes[i].methods[j].inputs_count; ++k) {
        print_variable(&classes[i].methods[j].inputs[k]);
        if (k != classes[i].methods[j].inputs_count-1 || classes[i].methods[j].outputs_count)
          printf(", ");
      }
      for (k = 0; k < classes[i].methods[j].outputs_count; ++k) {
        print_variable(&classes[i].methods[j].outputs[k]);
        if (k != classes[i].methods[j].outputs_count-1)
          printf(", ");
      }
      printf(");\n");
    }
    printf("};\n");
    if (i != count-1)
      printf("\n");
  }
}
