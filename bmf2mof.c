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

static void print_string(FILE *fout, char *str) {
  int len = strlen(str);
  int i;
  for (i=0; i<len; ++i) {
    if (str[i] == '"' || str[i] == '\\')
      fputc('\\', fout);
    fputc(str[i], fout);
  }
}

static void print_qualifiers(FILE *fout, struct mof_qualifier *qualifiers, uint32_t count, char *prefix) {
  uint32_t i;
  if (count > 0 || prefix) {
    fprintf(fout, "[");
    if (prefix) {
      fprintf(fout, "%s", prefix);
      if (count > 0)
        fprintf(fout, ", ");
    }
    for (i = 0; i < count; ++i) {
      switch (qualifiers[i].type) {
      case MOF_QUALIFIER_BOOLEAN:
        print_string(fout, qualifiers[i].name);
        if (!qualifiers[i].value.boolean)
          fprintf(fout, "(FALSE)");
        break;
      case MOF_QUALIFIER_SINT32:
        print_string(fout, qualifiers[i].name);
        fprintf(fout, "(%d)", qualifiers[i].value.sint32);
        break;
      case MOF_QUALIFIER_STRING:
        print_string(fout, qualifiers[i].name);
        fprintf(fout, "(\"");
        print_string(fout, qualifiers[i].value.string);
        fprintf(fout, "\")");
        break;
      default:
        fprintf(fout, "unknown");
        break;
      }
      if (qualifiers[i].toinstance || qualifiers[i].tosubclass || qualifiers[i].disableoverride || qualifiers[i].amended) {
        fprintf(fout, " :");
        if (qualifiers[i].toinstance)
          fprintf(fout, " ToInstance");
        if (qualifiers[i].tosubclass)
          fprintf(fout, " ToSubclass");
        if (qualifiers[i].disableoverride)
          fprintf(fout, " DisableOverride");
        if (qualifiers[i].amended)
          fprintf(fout, " Amended");
      }
      if (i != count-1)
        fprintf(fout, ", ");
    }
    fprintf(fout, "]");
  }
}

static void print_variable_type(FILE *fout, struct mof_variable *variable) {
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
  fprintf(fout, "%s", type ? type : "unknown");
}

static void print_variable(FILE *fout, struct mof_variable *variable, char *prefix) {
  if (variable->qualifiers_count > 0 || prefix) {
    print_qualifiers(fout, variable->qualifiers, variable->qualifiers_count, prefix);
    fprintf(fout, " ");
  }
  print_variable_type(fout, variable);
  fprintf(fout, " ");
  print_string(fout, variable->name);
  if (variable->variable_type == MOF_VARIABLE_BASIC_ARRAY || variable->variable_type == MOF_VARIABLE_OBJECT_ARRAY) {
    fprintf(fout, "[");
    if (variable->has_array_max)
      fprintf(fout, "%d", variable->array_max);
    fprintf(fout, "]");
  }
}

static void print_classes(FILE *fout, struct mof_class *classes, uint32_t count) {
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
      fprintf(fout, "#pragma namespace(\"");
      if (classes[i].namespace)
        print_string(fout, classes[i].namespace);
      else
        print_string(fout, "root\\default");
      fprintf(fout, "\")\n");
    }
    if (print_classflags) {
      fprintf(fout, "#pragma classflags(");
      if (classes[i].classflags == 1)
        fprintf(fout, "\"updateonly\"");
      else if (classes[i].classflags == 2)
        fprintf(fout, "\"createonly\"");
      else if (classes[i].classflags == 32)
        fprintf(fout, "\"safeupdate\"");
      else if (classes[i].classflags == 33)
        fprintf(fout, "\"updateonly\", \"safeupdate\"");
      else if (classes[i].classflags == 64)
        fprintf(fout, "\"forceupdate\"");
      else if (classes[i].classflags == 65)
        fprintf(fout, "\"updateonly\", \"forceupdate\"");
      else
        fprintf(fout, "%d", (int)classes[i].classflags);
      fprintf(fout, ")\n");
    }
    if (classes[i].qualifiers_count > 0) {
      print_qualifiers(fout, classes[i].qualifiers, classes[i].qualifiers_count, NULL);
      fprintf(fout, "\n");
    }
    fprintf(fout, "class ");
    print_string(fout, classes[i].name);
    fprintf(fout, " ");
    if (classes[i].superclassname) {
      fprintf(fout, ": ");
      print_string(fout, classes[i].superclassname);
      fprintf(fout, " ");
    }
    fprintf(fout, "{\n");
    for (j = 0; j < classes[i].variables_count; ++j) {
      fprintf(fout, "  ");
      print_variable(fout, &classes[i].variables[j], NULL);
      fprintf(fout, ";\n");
    }
    if (classes[i].variables_count && classes[i].methods_count)
      fprintf(fout, "\n");
    for (j = 0; j < classes[i].methods_count; ++j) {
      fprintf(fout, "  ");
      if (classes[i].methods[j].qualifiers_count > 0) {
        print_qualifiers(fout, classes[i].methods[j].qualifiers, classes[i].methods[j].qualifiers_count, NULL);
        fprintf(fout, " ");
      }
      if (classes[i].methods[j].return_value.variable_type)
        print_variable_type(fout, &classes[i].methods[j].return_value);
      else
        fprintf(fout, "void");
      fprintf(fout, " ");
      print_string(fout, classes[i].methods[j].name);
      fprintf(fout, "(");
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
        print_variable(fout, &classes[i].methods[j].parameters[k], direction);
        if (k != classes[i].methods[j].parameters_count-1)
          fprintf(fout, ", ");
      }
      fprintf(fout, ");\n");
    }
    fprintf(fout, "};\n");
    if (i != count-1)
      fprintf(fout, "\n");
  }
}
