-- DESCRIPTION: Verilator: VHDL process variables that read themselves in every assignment
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_proc_var_chain is
  port (a : in  std_logic_vector(7 downto 0);
        y : out std_logic_vector(7 downto 0));
end t_vhdl_proc_var_chain;

architecture behav of t_vhdl_proc_var_chain is
begin
  -- Each statement reads v twice, so a tracked copy of v's value would double per statement
  process (a)
    variable v : std_logic_vector(7 downto 0);
  begin
    v := a;
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    v := v xor std_logic_vector(shift_left(unsigned(v), 3));
    v := v xor std_logic_vector(shift_right(unsigned(v), 2));
    y <= v;
  end process;
end behav;
