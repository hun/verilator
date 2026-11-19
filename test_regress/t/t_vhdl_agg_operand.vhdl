-- DESCRIPTION: Verilator: an aggregate operand keeps the bounds its choices name
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's arith_cca adds a one-element aggregate to a wide vector
-- (`ac + bc + (0 to 0 => c)`), and misc_Delay binds an aggregate to a generic of a
-- declared unconstrained array type (`TAPS : natural_vector`) which a function then
-- indexes. numeric_std's arithmetic operators take unconstrained formals, so the
-- aggregate names its own range instead of having to cover the other operand's width.

library ieee;
use ieee.std_logic_1164.all;

package pkg is
  type natural_vector is array (natural range <>) of natural;
  function imax(a : natural_vector) return natural;
end package pkg;

package body pkg is
  function imax(a : natural_vector) return natural is
    variable res : natural := 0;
  begin
    for i in a'range loop
      if a(i) > res then
        res := a(i);
      end if;
    end loop;
    return res;
  end function imax;
end package body pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pkg.all;

entity sub is
  generic(
    TAPS : natural_vector
  );
  port(
    o_max : out std_logic_vector(31 downto 0)
  );
end entity sub;

architecture rtl of sub is
begin
  o_max <= std_logic_vector(to_unsigned(imax(TAPS), 32));
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pkg.all;

entity t is
  port(
    a : in std_logic_vector(7 downto 0);
    b : in std_logic_vector(7 downto 0);
    c : in std_logic;
    s : out std_logic_vector(7 downto 0);
    o_max : out std_logic_vector(31 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  s <= std_logic_vector(unsigned(a) + unsigned(b) + (0 to 0 => c));

  taps : entity work.sub
    generic map(
      TAPS => (1, 3, 5)
    )
    port map(
      o_max => o_max
    );
end architecture rtl;
