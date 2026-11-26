-- DESCRIPTION: Verilator: VHDL part of a record port that is not a bit vector is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package rp is type rec is record f : bit_vector; g : bit; end record; end package rp;
use work.rp.all;
entity sub is port (r : in rec); end entity sub;
architecture rtl of sub is begin end architecture rtl;
entity t is port (b : in bit_vector(1 downto 0); c : in bit); end entity t;
architecture rtl of t is
begin
  u : entity work.sub port map (r.f(0 to 0) => b, r.g => c);
end architecture rtl;
