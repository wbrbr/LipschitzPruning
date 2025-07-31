float sdf(vec3 p) {
    const int STACK_DEPTH = 32;
    float stack[STACK_DEPTH];
    int stack_idx = 0;

    for (int i = 0; i < total_num_nodes; i++) {
        Node node = nodes.tab[i];

        float d;
        if (node.type == NODETYPE_BINARY) {
            float left_val = stack[stack_idx-2];
            float right_val = stack[stack_idx-1];
            BinaryOp op = binary_ops.tab[node.idx_in_type];
            float k = BinaryOp_blend_factor(op);
            float s = BinaryOp_sign(op);
            uint typ = BinaryOp_op(op);
            if (typ == OP_SUB) right_val *= -1;
            stack_idx -= 2;
            d = s*(min(s*left_val, s*right_val) - kernel(abs(left_val-right_val), k));
        } else if (node.type == NODETYPE_PRIMITIVE) {
            Primitive prim = prims.tab[node.idx_in_type];
            d = eval_prim2(p, prim);
        }

        if (stack_idx >= STACK_DEPTH) {
            debugPrintfEXT("Stack overflow\n");
            return 1.0 / 0.0;
        }
        stack[stack_idx++] = d;
    }

    return stack[0];
}


float sdf_active(vec3 p, int cell_idx, out bool near_field) {
    int num_active = cells_num_active.tab[cell_idx];

    if (num_active == 0) {
        near_field = false;
        return cell_error_out.tab[cell_idx];
    }

    const int STACK_DEPTH = 32;
    float stack[STACK_DEPTH];
    int stack_idx = 0;

    int cell_offset = cells_offset.tab[cell_idx];

    for (int i = 0; i < num_active; i++) {
        ActiveNode active_node = active_nodes_out.tab[cell_offset + i];
        int node_idx = ActiveNode_index(active_node);

        Node node = nodes.tab[node_idx];
        float d;
        if (node.type == NODETYPE_BINARY) {
            float left_val = stack[stack_idx-2];
            float right_val = stack[stack_idx-1];
            stack_idx -= 2;
            BinaryOp op = binary_ops.tab[node.idx_in_type];
            float k = BinaryOp_blend_factor(op);
            float s = BinaryOp_sign(op);
            d = s*(min(s*left_val, s*right_val)-kernel(abs(left_val-right_val), k));
        } else if (node.type == NODETYPE_PRIMITIVE) {
            Primitive prim = prims.tab[node.idx_in_type];
            d = eval_prim2(p, prim);
        }

        d *= ActiveNode_sign(active_node) ? 1 : -1;
        if (stack_idx >= STACK_DEPTH) {
            debugPrintfEXT("Stack overflow\n");
            return 1.0 / 0.0;
        }
        stack[stack_idx++] = d;
    }

    near_field = (num_active > 0);

    return stack[0];
}