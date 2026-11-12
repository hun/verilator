-- DESCRIPTION: Verilator: VHDL generic subprogram parameter
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A generic function parameter; each instance substitutes a different actual
-- so a call to the formal `id` resolves to the bound function.
entity gen_buf is
    generic (function id(a : bit) return bit);
    port (
        i : in  bit;
        o : out bit
    );
end;

architecture rtl of gen_buf is
begin
    o <= id(i);
end;

entity t_vhdl_genfunc is
    port (
        i     : in  bit;
        o_not : out bit;
        o_id  : out bit
    );
end;

architecture struct of t_vhdl_genfunc is
    component gen_buf is
        generic (function id(a : bit) return bit);
        port (
            i : in  bit;
            o : out bit
        );
    end component;

    function inv(a : bit) return bit is
    begin
        return not a;
    end;

    function passthrough(a : bit) return bit is
    begin
        return a;
    end;
begin
    inv_inst : gen_buf generic map (id => inv) port map (i => i, o => o_not);
    id_inst : gen_buf generic map (id => passthrough) port map (i => i, o => o_id);
end;
