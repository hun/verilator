-- DESCRIPTION: Verilator: attribute specification and component in a generate part
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i  : in  std_logic;
    o  : out std_logic;
    o2 : out std_logic
  );
end entity t;

architecture rtl of t is
  -- The vendor tools read the attribute from the source; the frontend skips it
  attribute shreg_extract : string;
begin
  -- CHECK values: a for-generate carries an attribute specification over a signal of its
  -- own declarative part, the shift-register idiom
  delay_gen : for n in 0 to 0 generate
    signal tapped : std_logic;
    attribute shreg_extract of tapped : signal is "yes";
  begin
    tapped <= i;
    o <= tapped;
  end generate delay_gen;

  -- CHECK values: an if-generate declares the component its instantiation names
  pass_gen : if 1 = 1 generate
    component buf
      port (
        a : in  std_logic;
        y : out std_logic
      );
    end component buf;
  begin
    buf_inst : buf port map (a => i, y => o2);
  end generate pass_gen;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity buf is
  port (
    a : in  std_logic;
    y : out std_logic
  );
end entity buf;

library ieee;
use ieee.std_logic_1164.all;

architecture rtl of buf is
begin
  y <= not a;
end architecture rtl;
