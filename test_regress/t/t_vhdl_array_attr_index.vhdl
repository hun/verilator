-- DESCRIPTION: Verilator: VHDL index-range attributes A'LEFT(N) .. A'RANGE(N)
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The bracketed index names one of an array object's index ranges, so a
-- two-dimensional object can supply a subtype's bounds, a slice and a loop range
-- without spelling them out (IEEE 1076-2008 16.2.3: A'LEFT(N), A'RIGHT(N),
-- A'LOW(N), A'HIGH(N), A'LENGTH(N), A'ASCENDING(N), A'RANGE(N)).
-- Every value below is the one GHDL and nvc report for the same expression.

library ieee;
use ieee.std_logic_1164.all;

package attrpkg is
  subtype slv4 is std_logic_vector(3 downto 0);
  type tmat is array (natural range <>, natural range <>) of std_logic;
  type tvec is array (0 to 3) of slv4;
  constant m : tmat(1 downto 0, 3 downto 0) := ("1011", "0100");
  constant n : tmat(0 to 1, 3 downto 0) := ("0001", "1110");
  function pack (constant bits : tmat; constant row : natural) return slv4;
end package;

package body attrpkg is
  -- The folded loops walk the formal's own index ranges and write one bit of the
  -- result at a time, so `result(i)(j)` is a write into an element of an element
  function pack (constant bits : tmat; constant row : natural) return slv4 is
    variable result : tvec;
  begin
    result := (others => (others => '0'));
    for i in bits'range(1) loop
      for j in bits'high(2) downto bits'low(2) loop
        result(i)(j) := bits(i, j);
      end loop;
    end loop;
    return result(row);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.attrpkg.all;

entity t_vhdl_array_attr_index is
  port (
    clk  : in  std_logic;
    o0   : out slv4;
    o1   : out slv4;
    o2   : out slv4;
    full : out slv4;
    part : out std_logic_vector(1 downto 0);
    len  : out integer;
    lo   : out integer;
    hi   : out integer;
    lft  : out integer;
    rgt  : out integer;
    asc  : out std_logic;
    dsc  : out std_logic;
    ones : out integer
  );
end entity;

architecture rtl of t_vhdl_array_attr_index is
  -- A subtype whose bounds are the object's second index range
  signal copy : std_logic_vector(m'range(2));
begin
  copy <= "1101";
  o0 <= pack(m, 0);
  o1 <= pack(m, 1);
  o2 <= pack(m, 2);
  -- The same signal sliced by either of the two-dimensional constant's ranges
  full <= copy(m'range(2));
  part <= copy(m'range(1));
  len <= m'length(1) * 10 + m'length(2);
  lo <= m'low(2);
  hi <= m'high(1);
  lft <= m'left(2);
  rgt <= m'right(2);
  asc <= '1' when n'ascending(1) else '0';
  dsc <= '1' when n'ascending(2) else '0';

  -- A runtime loop over the same ranges, so the bounds are read at simulation time
  count : process (clk)
    variable acc : integer;
  begin
    if rising_edge(clk) then
      acc := 0;
      for k in m'range(1) loop
        for b in m'low(2) to m'high(2) loop
          if m(k, b) = '1' then
            acc := acc + 1;
          end if;
        end loop;
      end loop;
      ones <= acc;
    end if;
  end process;
end architecture;
