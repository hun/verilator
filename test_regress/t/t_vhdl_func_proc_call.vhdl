-- DESCRIPTION: Verilator: VHDL a procedure the function declares and calls
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_func_proc_call is
  generic (G : natural := 1);
  port (sel : in  std_logic_vector(3 downto 0);
        q   : out std_logic_vector(7 downto 0));
end t_vhdl_func_proc_call;

architecture behav of t_vhdl_func_proc_call is
  subtype byte_t is std_logic_vector(7 downto 0);
  type array_t is array (0 to 15) of byte_t;

  -- The procedures write the function's variables r, idx and big
  function build return array_t is
    variable r   : array_t := (others => x"ff");
    variable idx : integer := 0;
    variable big : integer := 0;

    procedure put (constant b : byte_t) is
    begin
      r (idx) := b;
      idx := idx + 1;
    end procedure;

    procedure fill (constant base : natural; constant n : natural) is
      variable step : integer := 1;
    begin
      for i in 0 to n - 1 loop
        r (idx) := std_logic_vector (to_unsigned (base + i * step, 8));
        idx := idx + 1;
      end loop;
      if n > 2 then
        big := big + 1;
      end if;
    end procedure;
  begin
    fill (10, 3);
    fill (100, 2);
    fill (7, 4);
    if G = 1 then
      fill (200, 2);
    end if;
    for j in 0 to 1 loop
      put (x"aa");
    end loop;
    r (15) := std_logic_vector (to_unsigned (big, 8));
    return r;
  end function;

  constant tbl : array_t := build;
begin
  q <= tbl (to_integer (unsigned (sel)));
end behav;
