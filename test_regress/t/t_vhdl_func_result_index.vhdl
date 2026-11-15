-- DESCRIPTION: Verilator: VHDL one element of an array a function returns
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Indexing a call result selects an element of the returned array, `row(c, 0)(0)`.
-- The returned value's own data type is a flat vector, so the element comes from the
-- declared return type. Every value below is the one GHDL and nvc report.

library ieee;
use ieee.std_logic_1164.all;

package retpkg is
  type t_vec is array (natural range <>) of std_logic_vector(3 downto 0);
  constant c : t_vec(1 downto 0) := ("0011", "1100");
  function row (x : t_vec; i : natural) return t_vec;
end package;

package body retpkg is
  function row (x : t_vec; i : natural) return t_vec is
    variable r : t_vec(x'range);
  begin
    for k in x'range loop
      r(k) := x(k);
    end loop;
    return r;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.retpkg.all;

entity t_vhdl_func_result_index is
  port (
    o : out std_logic_vector(3 downto 0);
    p : out std_logic_vector(3 downto 0);
    n : out integer
  );
end entity;

architecture rtl of t_vhdl_func_result_index is
begin
  -- c(0) is the aggregate's second element, as the index range is 1 downto 0
  o <= row(c, 0)(0);
  p <= row(c, 0)(1);
  n <= c'length;
end architecture;