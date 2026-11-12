-- DESCRIPTION: Verilator: VHDL record-element port associations
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package pkg is
  type rec_t is record
    a : std_logic_vector(3 downto 0);
    b : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.pkg.all;

entity dut is
  port (
    ri : in  rec_t;
    ro : out rec_t
  );
end;

architecture rtl of dut is
begin
  ro.a <= ri.a and ri.b;
  ro.b <= ri.a or ri.b;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.pkg.all;

entity t is
  port (
    ai : in  std_logic_vector(3 downto 0);
    bi : in  std_logic_vector(3 downto 0);
    oa : out std_logic_vector(3 downto 0);
    ob : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- Each element of the record ports is associated with its own actual
  u : entity work.dut
    port map (ri.a => ai, ri.b => bi, ro.a => oa, ro.b => ob);
end;
