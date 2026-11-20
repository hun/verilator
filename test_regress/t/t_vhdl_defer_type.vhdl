-- DESCRIPTION: Verilator: a type naming a deferred subtype waits for the constant behind it
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package defer_pkg is
  -- An if/else body is not a single return, so the pre-scan cannot fold the call and the
  -- constant it sizes has to wait for the emitter
  function width_of(v : integer) return positive;
end package defer_pkg;

package body defer_pkg is
  function width_of(v : integer) return positive is
  begin
    if v > 3 then
      return 4;
    else
      return 2;
    end if;
  end function width_of;
end package body defer_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.defer_pkg.all;

entity t is
  port (
    o_len : out integer;
    o_sum : out integer
  );
end entity t;

architecture rtl of t is
begin

  blk : block
    constant w_c : positive := width_of(4);
    -- Deferred: its constraint names w_c, which has no value until the block is emitted
    subtype word_t is std_ulogic_vector(w_c - 1 downto 0);
    -- Deferred too: its element is the subtype above, which is not registered yet
    type row_t is array (natural range <>) of word_t;

    signal rows : row_t(0 to 1) := (others => (others => '0'));
    signal a, b : word_t;
  begin

    a      <= (others => '1');
    b      <= (others => '0');
    rows(0) <= a;
    rows(1) <= b;

    -- The length comes from the deferred subtype's constraint, the sum from the array of it
    o_len <= word_t'length;
    o_sum <= to_integer(unsigned(rows(0))) + to_integer(unsigned(rows(1)));

  end block blk;

end architecture rtl;