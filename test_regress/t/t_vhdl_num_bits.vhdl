-- DESCRIPTION: Verilator: static unsigned vector function in a subtype range
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package num_bits_pkg is
  function bits_needed(value : natural) return positive;
end package;

package body num_bits_pkg is
  function bits_needed(value : natural) return positive is
    constant v : unsigned(7 downto 0) := to_unsigned(value, 8);
  begin
    for i in v'high downto v'low loop
      if v(i) = '1' then
        return i + 1;
      end if;
    end loop;
    return 1;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.num_bits_pkg.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    o : out std_logic_vector(3 downto 0);
    vi_o : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  constant W : natural := 5;
  subtype addr_t is unsigned(bits_needed(W) - 1 downto 0);
  signal addr : addr_t;
  signal vi : unsigned(3 downto 0);

  function to_int(value : boolean) return natural is
  begin
    if value then
      return 1;
    end if;
    return 0;
  end;
begin
  addr <= unsigned(a(2 downto 0));
  o <= std_logic_vector(resize(addr, 4));
  -- unsigned + natural keeps the vector width
  vi <= unsigned(a) + to_int(a(0) = '1');
  vi_o <= std_logic_vector(vi);
end;
