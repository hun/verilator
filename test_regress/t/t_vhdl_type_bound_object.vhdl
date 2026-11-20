-- DESCRIPTION: Verilator: type bounds that name an object or a folded constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An element subtype may take its range from an object of the same declarative
-- part, and a type in a subprogram may be bounded by a constant of that
-- subprogram whose value comes from another package's function.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package util is
  function twice(a : natural) return natural;
end package;

package body util is
  function twice(a : natural) return natural is
  begin
    return 2 * a;
  end function;
end package body;

package pk is
  function doubled(v : unsigned) return unsigned;
end package;

package body pk is
  use work.util.all;

  function doubled(v : unsigned) return unsigned is
    constant w  : natural := v'length;
    constant pw : natural := twice(w);

    type pairs_t is array (natural range <>) of unsigned(pw - 1 downto 0);

    variable pairs : pairs_t(0 to 1) := (others => (others => '0'));
  begin
    pairs(0) := v & v;
    pairs(1) := pairs(0) xor (pairs(0) ror 1);
    return pairs(1)(w - 1 downto 0);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pk.all;

entity t is
  port (
    din  : in  std_logic_vector(3 downto 0);
    dout : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
  signal data : std_logic_vector(3 downto 0);
  type row_t is array (natural range <>) of std_logic_vector(data'range);
  signal rows : row_t(0 to 1) := (others => (others => '0'));
begin
  data    <= din;
  rows(0) <= data;
  rows(1) <= not data;
  dout    <= std_logic_vector(rows(1) xor std_logic_vector(doubled(unsigned(data))));
end architecture;
