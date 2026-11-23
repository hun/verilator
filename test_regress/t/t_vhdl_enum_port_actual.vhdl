-- DESCRIPTION: Verilator: VHDL enumeration and boolean port actuals of an instance
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package t_pkg is
  type sel_t is (a_16, b_32, c_64);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.t_pkg.all;

entity child is
  port (
    s  : in  sel_t;
    en : in  boolean;
    o  : out std_logic_vector(1 downto 0);
    ob : out std_logic
  );
end entity;

architecture rtl of child is
begin
  o <= "00" when s = a_16 else
       "01" when s = b_32 else
       "10";
  ob <= '1' when en else '0';
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.t_pkg.all;

entity t is
  generic (
    g  : sel_t := b_32;
    gb : boolean := true
  );
  port (
    x   : in  std_logic;
    o1  : out std_logic_vector(1 downto 0);
    o2  : out std_logic_vector(1 downto 0);
    ob1 : out std_logic;
    ob2 : out std_logic
  );
end entity;

architecture rtl of t is
  component child is
    port (
      s  : in  sel_t;
      en : in  boolean;
      o  : out std_logic_vector(1 downto 0);
      ob : out std_logic
    );
  end component;
  signal sg : sel_t;
begin
  sg <= c_64 when x = '1' else a_16;
  u1 : child port map (s => g, en => gb, o => o1, ob => ob1);
  u2 : child port map (s => sg, en => x = '1', o => o2, ob => ob2);
end architecture;
