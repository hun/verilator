-- DESCRIPTION: Verilator: VHDL index constraint lists and an open index
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A nested array takes one constraint per index, either as a comma list
-- `t(v'range, n - 1 downto 0)` or chained `t(v'range)(n - 1 downto 0)`. `open` leaves an
-- index to the object that names the subtype, so `t(open)(1 downto 0)` constrains only
-- the element. Every value below is the one GHDL and nvc report.

library ieee;
use ieee.std_logic_1164.all;

package idxpkg is
  constant k : integer := 4;
  type tmat is array (natural range <>, natural range <>) of std_logic;
  type t_slm is array (natural range <>) of std_logic_vector;
  subtype t_slm2 is t_slm(open)(1 downto 0);
  function pack (v : tmat) return std_logic_vector;
end package;

package body idxpkg is
  function pack (v : tmat) return std_logic_vector is
    -- The first index comes from the actual's range, the second is given
    variable r : tmat(v'range, k - 1 downto 0);
  begin
    r := (others => (others => '0'));
    for i in v'range loop
      for j in 0 to k - 1 loop
        r(i, j) := v(i, j);
      end loop;
    end loop;
    return r(1, 3) & r(1, 2) & r(1, 1) & r(1, 0);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.idxpkg.all;

entity t_vhdl_index_constraints is
  port (
    o : out std_logic_vector(k - 1 downto 0);
    p : out std_logic_vector(3 downto 0);
    n : out integer
  );
end entity;

architecture rtl of t_vhdl_index_constraints is
  -- m(1) is the aggregate's first element, as the index range is 1 downto 0
  constant m : tmat(1 downto 0, 3 downto 0) := ("1011", "0100");
  signal s : t_slm2(0 to 1);
begin
  s(0) <= "01";
  s(1) <= "10";
  o <= pack(m);
  p <= s(1) & s(0);
  n <= s'length;
end architecture;
