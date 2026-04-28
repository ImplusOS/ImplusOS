#include "XMLParser.h"
#include "Memory.h"
#include <string.h>

static void skip_whitespace(const char **s) {
    while (**s == ' ' || **s == '\t' || **s == '\r' || **s == '\n') {
        (*s)++;
    }
}

static bool parse_name(const char **s, char *out, uint32_t max_len) {
    uint32_t i = 0;
    while (**s && **s != ' ' && **s != '\t' && **s != '\r' && **s != '\n' &&
           **s != '=' && **s != '>' && **s != '/' && **s != '<') {
        if (i < max_len - 1) {
            out[i++] = **s;
        }
        (*s)++;
    }
    out[i] = '\0';
    return i > 0;
}

static bool parse_string(const char **s, char *out, uint32_t max_len) {
    char quote = **s;
    if (quote != '"' && quote != '\'') return false;
    (*s)++;
    
    uint32_t i = 0;
    while (**s && **s != quote) {
        if (i < max_len - 1) {
            out[i++] = **s;
        }
        (*s)++;
    }
    out[i] = '\0';
    if (**s == quote) (*s)++;
    return true;
}

static xml_node_t *parse_node(const char **s) {
    skip_whitespace(s);
    if (**s != '<') return NULL;
    (*s)++;

    if (**s == '?' || **s == '!') {
        while (**s && **s != '>') (*s)++;
        if (**s == '>') (*s)++;
        return parse_node(s);
    }
    
    if (**s == '/') {
        return NULL;
    }

    xml_node_t *node = (xml_node_t *)malloc(sizeof(xml_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(xml_node_t));

    if (!parse_name(s, node->tag, sizeof(node->tag))) {
        free(node);
        return NULL;
    }

    while (1) {
        skip_whitespace(s);
        if (**s == '>' || **s == '/' || **s == '\0') break;

        char attr_name[XML_MAX_ATTR_NAME];
        if (!parse_name(s, attr_name, sizeof(attr_name))) break;

        skip_whitespace(s);
        if (**s == '=') {
            (*s)++;
            skip_whitespace(s);
            char attr_val[XML_MAX_ATTR_VALUE];
            if (parse_string(s, attr_val, sizeof(attr_val))) {
                if (node->attr_count < XML_MAX_ATTRIBUTES) {
                    os_strcpy_s(node->attributes[node->attr_count].name, XML_MAX_ATTR_NAME, attr_name);
                    os_strcpy_s(node->attributes[node->attr_count].value, XML_MAX_ATTR_VALUE, attr_val);
                    node->attr_count++;
                }
            }
        }
    }

    if (**s == '/') {
        (*s)++;
        skip_whitespace(s);
        if (**s == '>') (*s)++;
        return node;
    }

    if (**s == '>') {
        (*s)++;
        
        while (1) {
            skip_whitespace(s);
            if (**s == '\0') break;
            
            if (**s == '<') {
                if (*(*s + 1) == '/') {
                    (*s) += 2;
                    char end_tag[XML_MAX_TAG_LEN];
                    parse_name(s, end_tag, sizeof(end_tag));
                    skip_whitespace(s);
                    if (**s == '>') (*s)++;
                    break;
                } else {
                    xml_node_t *child = parse_node(s);
                    if (child) {
                        if (node->child_count < XML_MAX_CHILDREN) {
                            node->children[node->child_count++] = child;
                        } else {
                            xml_free(child);
                        }
                    }
                }
            } else {
                uint32_t i = 0;
                while (**s && **s != '<') {
                    if (i < sizeof(node->text) - 1) {
                        node->text[i++] = **s;
                    }
                    (*s)++;
                }
                node->text[i] = '\0';
                
                while(i > 0 && (node->text[i-1] == ' ' || node->text[i-1] == '\t' || node->text[i-1] == '\r' || node->text[i-1] == '\n')) {
                    i--;
                }
                node->text[i] = '\0';
            }
        }
    }

    return node;
}

xml_node_t *xml_parse(const char *xml_str) {
    if (!xml_str) return NULL;
    const char *s = xml_str;
    return parse_node(&s);
}

void xml_free(xml_node_t *node) {
    if (!node) return;
    for (uint32_t i = 0; i < node->child_count; i++) {
        xml_free(node->children[i]);
    }
    free(node);
}

const char *xml_get_attr(const xml_node_t *node, const char *name) {
    if (!node || !name) return NULL;
    for (uint32_t i = 0; i < node->attr_count; i++) {
        if (strcmp(node->attributes[i].name, name) == 0) {
            return node->attributes[i].value;
        }
    }
    return NULL;
}

xml_node_t *xml_find_child(const xml_node_t *node, const char *tag) {
    if (!node || !tag) return NULL;
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (strcmp(node->children[i]->tag, tag) == 0) {
            return node->children[i];
        }
    }
    return NULL;
}
