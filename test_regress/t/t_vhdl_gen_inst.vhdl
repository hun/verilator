-- DESCRIPTION: Verilator: VHDL for generate with an instance and local signal
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity bit_inv is
  port (
    x : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of bit_inv is
begin
  y <= not x;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  g : for i in 0 to 3 generate
    signal s : std_logic;
  begin
    u1 : entity work.bit_inv port map (x => a(i), y => s);
    y(i) <= s;
  end generate;
end;
