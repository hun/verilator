-- DESCRIPTION: Verilator: VHDL function locals and actuals read more than once
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_func_local_chain is
  port (a  : in  std_logic_vector(7 downto 0);
        yc : out std_logic_vector(7 downto 0);
        yp : out std_logic_vector(7 downto 0);
        yn : out std_logic_vector(7 downto 0));
end t_vhdl_func_local_chain;

architecture behav of t_vhdl_func_local_chain is
  -- Each statement reads v twice, so a value kept as an expression doubles per statement
  function hash (x : std_logic_vector (7 downto 0)) return std_logic_vector is
    variable v : std_logic_vector (7 downto 0) := x;
  begin
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    return v;
  end hash;

  -- The formal is read twice, so an actual that calls this again doubles per nesting
  function twice (x : std_logic_vector (7 downto 0)) return std_logic_vector is
  begin
    return std_logic_vector (unsigned (x) + unsigned (x) + 1);
  end twice;
begin
  yc <= hash (a);
  yn <= twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (twice (a))))))))))))))));
  process (a)
    variable r : std_logic_vector (7 downto 0);
  begin
    r := hash (a);
    yp <= r;
  end process;
end behav;
